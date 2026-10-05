#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBoxLookahead.h>
#include <SubtitleCutPaste.h>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleBoxLookaheadTests) {
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {1,sequence,sequence,sequence*1000,2,3,4};
    }
    static SubtitleBoxObservation Cue(uint64_t sequence, bool twoLines=false) {
        SubtitleBoxObservation o;
        o.identity=Identity(sequence); o.analyzed=o.barAuthority=true;
        o.width=640; o.height=360; o.pictureTop=45; o.pictureBottom=315;
        o.text.detected=true; o.text.lineCount=twoLines?2:1;
        o.text.bounds={196,314,444,twoLines?352:336};
        o.text.lineBounds[0]={200,318,440,332};
        o.text.lineSignatures[0].fill(0x55);
        if(twoLines) {
            o.text.lineBounds[1]={240,338,400,348};
            o.text.lineSignatures[1].fill(0x33);
        }
        // Handcrafted grouping fixtures represent two physically present rows,
        // with the optional companion sometimes missed by segmentation.
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=640;ink->height=360;ink->step=1;ink->sourceRows.resize(360);
        ink->rawInk.resize((640*360+63)/64);ink->ownedInk.resize(ink->rawInk.size());
        for(int y=0;y<360;++y)ink->sourceRows[y]=y;
        const SubtitleBoxRect rows[2]={{200,318,440,332},{240,338,400,348}};
        for(int line=0;line<2;++line)for(int y=rows[line].top;y<rows[line].bottom;++y)
            for(int x=rows[line].left;x<rows[line].right;++x)if((x-rows[line].left)%9<5) {
                const size_t pixel=size_t(y)*640+x;
                ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
                if(line==0 || twoLines)ink->ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
        o.ink=ink;
        return o;
    }
    static SubtitleBoxObservation LowerOnlyCue(uint64_t sequence) {
        auto o=Cue(sequence,false);
        o.text.bounds={240,338,400,348};
        o.text.lineBounds[0]={240,338,400,348};
        o.text.lineSignatures[0].fill(0x33);
        return o;
    }
    static void AddNewGlyph(SubtitleInkSnapshot& ink,int left,int top,bool hShape=false) {
        for(int y=0;y<14;++y)for(int x=0;x<8;++x) {
            const bool pixel=hShape ? (x<2 || x>=6 || (y>=6 && y<=7)) :
                (x<2 || x>=6 || y<2 || y>=12);
            if(!pixel)continue;
            const size_t p=size_t(top+y)*ink.width+left+x;
            ink.rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
    }
    static SubtitleBoxPreview Resolve(std::initializer_list<SubtitleBoxObservation> frames) {
        return SubtitleBoxLookahead::Resolve(frames.begin(),frames.size(),7,8);
    }
    static void Fill(std::vector<uint16_t>& f,int left,int top,int right,int bottom,int code) {
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) f[y*640+x]=uint16_t(code<<6);
    }
    static void Text(std::vector<uint16_t>& f,int left,int top,int count,bool changed=false) {
        for(int n=0;n<count;++n) {
            const int x=left+13*n;
            Fill(f,x-1,top-1,x+9,top+15,64);
            if(!changed) {
                Fill(f,x,top,x+8,top+14,700); Fill(f,x+2,top+2,x+6,top+12,64);
            } else {
                Fill(f,x,top,x+2,top+14,700); Fill(f,x+6,top,x+8,top+14,700);
                Fill(f,x+2,top+6,x+6,top+8,700);
            }
        }
    }
    static std::vector<uint16_t> Pixels(bool full=false) {
        std::vector<uint16_t> f(640*360*3/2,uint16_t(512<<6));
        Fill(f,0,0,640,360,64); Fill(f,0,full?0:45,640,full?360:315,300); return f;
    }
    static AnalysisLumaSource Source(std::vector<uint16_t>& pixels) {
        AnalysisLumaSource source;
        source.data=reinterpret_cast<const uint8_t*>(pixels.data());source.dataBytes=pixels.size()*2;
        source.width=640;source.height=360;source.rowBytes=source.chromaRowBytes=1280;
        source.format=AnalysisLumaFormat::P010;source.generation=2;
        return source;
    }
    static SubtitleBoxObservation Measure(std::vector<uint16_t>& pixels,uint64_t sequence) {
        const auto source=Source(pixels);
        SubtitleBoxDetector scanner;
        return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),Identity(sequence),false);
    }
    static std::vector<uint16_t> WeakBottomPixels(bool glyphs=true) {
        auto pixels=Pixels();Fill(pixels,0,0,640,360,64);
        // Keep independently strong top geometry, but only sixteen scattered
        // bright columns reveal the established bottom edge in the dark scene.
        Fill(pixels,0,45,640,100,300);
        for(int i=4;i<128;i+=8) {
            const int x=(640*(4*128+96*(2*i+1)))/(200*128);
            Fill(pixels,x,300,x+1,315,90);
        }
        if(glyphs)Text(pixels,240,310,12);
        return pixels;
    }
    static SubtitleBoxObservation MeasureWithBar(std::vector<uint16_t>& pixels,uint64_t sequence,
        const SubtitleBarTrackingReference& reference,uint64_t policy=0,uint64_t continuity=0) {
        const auto source=Source(pixels);SubtitleBoxDetector scanner;
        return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),
            Identity(sequence),false,reference,policy,continuity);
    }
    static AnalysisLumaSource RgbSource(std::vector<uint8_t>& pixels,int width,int height) {
        AnalysisLumaSource source;source.data=pixels.data();source.dataBytes=pixels.size();
        source.width=width;source.height=height;source.rowBytes=width*4;
        source.format=AnalysisLumaFormat::NativeRgb;source.encoding=VideoFrameEncoding::BGRA_8BIT;
        source.colorspace=ColorSpace::REC_709;source.generation=2;return source;
    }
public:
    TEST_METHOD(MovingInterGlyphPicturePreservesCompleteCueDuringRegrouping) {
        auto first=Cue(1,true),second=Cue(2,true);
        first.pictureBottom=second.pictureBottom=329;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,second}));
        Assert::IsTrue(acquired.detected);
        for(uint64_t sequence=2;sequence<62;++sequence) {
            auto current=LowerOnlyCue(sequence);
            current.pictureBottom=329;
            auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
            // Picture texture changes in every inter-glyph gap. None of these
            // short fragments is a glyph, and all acquired glyphs stay exact.
            for(int gap=0;gap<26;++gap)for(int dy=0;dy<6;++dy)for(int dx=0;dx<4;++dx) {
                const int y=318+int(sequence%2)*5+dy,x=205+gap*9+dx;
                const size_t pixel=size_t(y)*ink->width+x;
                ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
            current.ink=ink;
            Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,current));
            auto next=current;next.identity=Identity(sequence+1);
            const auto result=tracker.Consume(Resolve({current,next}));
            Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
            Assert::AreEqual(2,result.lineCount);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
        }
    }
    TEST_METHOD(RealNewSecondLineExpandsCurrentCueAndBlankFrameStopsIt) {
        auto single=Pixels();Text(single,175,307,22);
        auto doubleLine=single;Text(doubleLine,240,336,12);
        auto blank=Pixels();
        const auto first=Measure(single,1),lock=Measure(single,2);
        const auto expanded=Measure(doubleLine,2),confirmed=Measure(doubleLine,3);
        const auto gone=Measure(blank,3);
        Assert::AreEqual(1,first.text.lineCount);
        Assert::AreEqual(2,expanded.text.lineCount);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        const auto complete=tracker.Consume(Resolve({expanded,confirmed}));
        Assert::IsTrue(complete.detected);Assert::AreEqual(2,complete.lineCount);
        Assert::AreEqual(acquired.cue,complete.cue);
        Assert::IsTrue(complete.bounds.bottom>acquired.bounds.bottom);
        auto returning=Measure(doubleLine,4);
        Assert::IsFalse(tracker.Consume(Resolve({gone,returning})).detected,
            L"future return cannot justify moving text absent from current pixels");
    }
    TEST_METHOD(CurrentGlyphsRemainMovedThroughLastFiveFramesBeforeDifferentCue) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto changed=Pixels();Text(changed,175,307,22,true);Text(changed,240,336,12,true);
        SubtitleBoxPresentation tracker;SubtitleCutPastePresentation placement;
        const auto first=Measure(pixels,1),lock=Measure(pixels,2);
        const auto acquiredPreview=Resolve({first,lock});
        const auto acquired=tracker.Consume(acquiredPreview);
        const auto originalPlacement=placement.Consume(acquired,acquiredPreview);
        Assert::IsTrue(originalPlacement.valid);
        for(uint64_t sequence=2;sequence<=6;++sequence) {
            auto current=Measure(pixels,sequence);current.text={};
            // The queue has already crossed the subtitle boundary. There is
            // no future old grouping available to authorize a timed bridge.
            auto next=Measure(changed,sequence+1);
            const auto preview=Resolve({current,next});
            const auto result=tracker.Consume(preview);
            const auto moved=placement.Consume(result,preview);
            Assert::IsTrue(result.detected && result.held && moved.valid);
            Assert::AreEqual(acquired.cue,result.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(originalPlacement.destination,moved.destination,0));
        }
        auto next=Measure(changed,7),confirmed=Measure(changed,8);
        const auto replacement=tracker.Consume(Resolve({next,confirmed}));
        Assert::IsTrue(replacement.detected);Assert::IsTrue(replacement.cue!=acquired.cue);
        auto blank=Pixels();
        Assert::IsFalse(tracker.Consume(Resolve({Measure(blank,8),Measure(changed,9)})).detected);
    }
    TEST_METHOD(SegmentationMissWithNewSecondLineCannotReuseIncompleteOldCue) {
        auto single=Pixels();Text(single,175,307,22);
        auto both=single;Text(both,240,336,12);
        const auto first=Measure(single,1),lock=Measure(single,2);
        auto current=Measure(both,2);current.text={};
        SubtitleBoxRect novelLine;
        Assert::IsTrue(SubtitleBoxLookahead::FindNewInkExtension(*first.ink,*current.ink,
            first.text.lineBounds[0],first.width,first.height,novelLine),
            L"the current second line must be recognized as an unconfirmed glyph extension");
        SubtitleBoxPresentation tracker;
        Assert::IsTrue(tracker.Consume(Resolve({first,lock})).detected);
        auto oldReturns=Measure(single,3);
        Assert::IsFalse(tracker.Consume(Resolve({current,oldReturns})).detected,
            L"new current glyphs outside the reference require complete reacquisition");
    }
    TEST_METHOD(UnchangedGroupingStillDiscoversAndTracksAnActualNewSecondLine) {
        auto single=Pixels();Text(single,175,307,22);
        auto both=single;Text(both,240,336,12);
        const auto first=Measure(single,1),lock=Measure(single,2);
        auto current=Measure(both,2),next=Measure(both,3);
        const auto completeBounds=current.text.bounds;
        // Simulate repeated detector under-grouping while raw current pixels
        // contain a newly appeared second line on every frame.
        current.text=first.text;next.text=first.text;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        auto next2=next;next2.identity=Identity(4);next2.text=first.text;
        auto next3=next;next3.identity=Identity(5);next3.text=first.text;
        const auto expanded=tracker.Consume(Resolve({current,next,next2,next3}));
        Assert::IsTrue(expanded.detected);Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.bottom>=completeBounds.bottom-4);
        Assert::IsTrue(expanded.bounds.bottom>acquired.bounds.bottom);
        const auto retained=tracker.Consume(Resolve({next}));
        Assert::IsTrue(retained.detected);Assert::AreEqual(expanded.cue,retained.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(expanded.bounds,retained.bounds,0));
        auto removed=Measure(single,4),stillRemoved=Measure(single,5);
        const auto shortened=tracker.Consume(Resolve({removed,stillRemoved}));
        Assert::IsTrue(shortened.detected);Assert::IsTrue(shortened.cue!=expanded.cue);
        Assert::IsTrue(shortened.bounds.bottom<expanded.bounds.bottom);
        SubtitleBoxPresentation unconfirmed;
        unconfirmed.Consume(Resolve({first,lock}));
        const auto awaiting=unconfirmed.Consume(Resolve({current,next}));
        Assert::IsTrue(awaiting.detected);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,awaiting.bounds,0),
            L"unconfirmed scene candidates cannot resize or revoke the validated current cue");
    }
    TEST_METHOD(NormalDetectedLocalGlyphChangeCannotReuseCoarseCueIdentity) {
        auto before=Pixels();Text(before,175,307,22);Text(before,240,336,12);
        auto changed=before;Text(changed,175+13*10,307,1,true);
        const auto first=Measure(before,1),lock=Measure(before,2);
        const auto current=Measure(changed,2),next=Measure(changed,3);
        Assert::IsTrue(current.text.detected);
        Assert::IsTrue(SubtitleBoxLookahead::SameCue(first,current),
            L"one changed letter can fit the coarse whole-line signature tolerance");
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(first,current));
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        const auto replacement=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(replacement.detected);Assert::IsTrue(replacement.cue!=acquired.cue);
    }
    TEST_METHOD(OneSampleStemThickeningIsNotANewDetachedCharacter) {
        auto first=Cue(1,true),current=LowerOnlyCue(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        // Captured antialiasing can add a one-sample column alongside an
        // unchanged stem. The original stem remains present at every row.
        for(int y=320;y<329;++y) {
            const size_t pixel=size_t(y)*ink->width+205;
            ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
        current.ink=ink;
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,current));
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,Cue(2,true)}));
        const auto held=tracker.Consume(Resolve({current}));
        Assert::IsTrue(held.detected);Assert::AreEqual(acquired.cue,held.cue);
    }
    TEST_METHOD(IsolatedStableSceneComponentsCannotInventALineOrDistantWord) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        auto current=Cue(2,true),next=Cue(3,true);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        AddNewGlyph(*ink,310,292); // One centered bright shape above the row.
        AddNewGlyph(*ink,100,318); // Baseline aligned, implausibly distant.
        current.ink=ink;next.ink=ink;
        const auto result=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
        auto extendedInk=std::make_shared<SubtitleInkSnapshot>(*ink);
        AddNewGlyph(*extendedInk,448,318); // A legitimate nearby extension.
        current.ink=extendedInk;next.ink=extendedInk;
        current.identity=Identity(3);next.identity=Identity(4);
        const auto extended=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(extended.detected);Assert::IsTrue(extended.bounds.right>acquired.bounds.right);
        Assert::AreEqual(acquired.bounds.left,extended.bounds.left,
            L"a valid nearby extension cannot lend support to a disconnected distant scene component");
    }
    TEST_METHOD(NewGlyphInsideOldLineWhitespaceRejectsCachedIdentity) {
        auto first=Cue(1,true),current=LowerOnlyCue(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        // A new full-height narrow character occupies an old gap. It lies
        // outside all acquired component envelopes but inside the old line.
        for(int y=318;y<332;++y)for(int x=206;x<208;++x) {
            const size_t pixel=size_t(y)*ink->width+x;
            ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
        current.ink=ink;
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(first,current));
        SubtitleBoxPresentation tracker;
        Assert::IsTrue(tracker.Consume(Resolve({first,Cue(2,true)})).detected);
        current.text={};
        Assert::IsFalse(tracker.Consume(Resolve({current,Cue(3,true)})).detected);
    }
    TEST_METHOD(NewStrokeInsideAcquiredGlyphHoleRejectsCachedIdentity) {
        auto before=Pixels();Text(before,175,307,22);Text(before,240,336,12);
        auto after=before;
        // Fill one letter's previously empty interior without removing any
        // old stroke. Coverage alone would incorrectly accept this edit.
        Fill(after,175+13*10+2,309,175+13*10+6,319,700);
        const auto reference=Measure(before,1),current=Measure(after,2);
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(reference,current));
    }
    TEST_METHOD(FreshAcceptedCueAfterBridgeDoesNotRetainHeldDiagnosticFlag) {
        SubtitleBoxPresentation tracker;
        const auto first=tracker.Consume(Resolve({Cue(1),Cue(2)}));
        auto miss=Cue(2);miss.text={};
        const auto held=tracker.Consume(Resolve({miss,Cue(3)}));
        Assert::IsTrue(held.detected && held.held);
        const auto fresh=tracker.Consume(Resolve({Cue(3,true),Cue(4)}));
        Assert::IsTrue(fresh.detected);Assert::IsFalse(fresh.held);
        Assert::AreEqual(first.cue,fresh.cue);Assert::AreEqual(1,fresh.lineCount);
    }
    TEST_METHOD(OneSegmentationMissBridgesOnlyWithCurrentInkAndQueuedReturn) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto first=Measure(pixels,1),second=Measure(pixels,2),third=Measure(pixels,3);
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,second));
        auto miss=second;miss.text={};
        SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(Resolve({first,second}));
        const auto held=tracker.Consume(Resolve({miss,third}));
        Assert::IsTrue(held.detected && held.held);Assert::AreEqual(acquired.cue,held.cue);
        Assert::AreEqual(acquired.observations,held.observations,L"rescue is not segmentation confirmation");
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,held.bounds,0));
        const auto returned=tracker.Consume(Resolve({third}));
        Assert::IsTrue(returned.detected && !returned.held);Assert::AreEqual(acquired.cue,returned.cue);
    }
    TEST_METHOD(ExactCurrentInkSurvivesLookaheadCueBoundaryButConfirmedChangedTextDoesNot) {
        auto oldPixels=Pixels();Text(oldPixels,175,307,22);Text(oldPixels,240,336,12);
        const auto first=Measure(oldPixels,1),lockedFrame=Measure(oldPixels,2),
            current=Measure(oldPixels,3);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lockedFrame}));
        Assert::IsTrue(acquired.detected);
        Assert::AreEqual(acquired.cue,tracker.Consume(Resolve({lockedFrame})).cue);

        auto changedPixels=Pixels();Text(changedPixels,175,307,22,true);Text(changedPixels,240,336,12);
        const auto changed=Measure(changedPixels,4),changedNext=Measure(changedPixels,5);
        const auto boundary=Resolve({current,changed});
        Assert::AreEqual(1u,boundary.matchingFrames,L"the next frame starts a different cue");
        Assert::IsFalse(boundary.currentLinesConfirmed);
        const auto retained=tracker.Consume(boundary);
        Assert::IsTrue(retained.detected,L"exact current pixels still prove the established cue");
        Assert::AreEqual(acquired.cue,retained.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,retained.bounds,0));

        const auto newCue=tracker.Consume(Resolve({changed,changedNext}));
        Assert::IsTrue(newCue.detected,L"two frames confirm the changed text");
        Assert::IsTrue(newCue.cue!=acquired.cue,L"changed fixed-coordinate glyphs break the old cue");
    }
    TEST_METHOD(ExactCurrentGlyphValidationOutlivesSegmentationBudgetWithoutRepeatConfirmation) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3),d=Measure(pixels,4),e=Measure(pixels,5);
        SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(Resolve({a,b}));
        b.text={};c.text={};
        const auto one=tracker.Consume(Resolve({b,c,d}));
        Assert::IsTrue(one.detected && one.held);
        const auto repeat=tracker.Consume(Resolve({b,c,d}));
        Assert::AreEqual(one.cue,repeat.cue);Assert::AreEqual(one.observations,repeat.observations);
        const auto two=tracker.Consume(Resolve({c,d}));Assert::IsTrue(two.detected && two.held);
        d.text={};
        const auto third=tracker.Consume(Resolve({d,e}));
        Assert::IsTrue(third.detected && third.held,
            L"fresh exact current glyph proof does not expire with the segmentation bridge budget");
        Assert::AreEqual(acquired.cue,third.cue);
        Assert::AreEqual(acquired.observations,third.observations);
        Assert::IsTrue(acquired.detected);
    }
    TEST_METHOD(UnconfirmedExpandedGroupingMayBridgeButConfirmedNewTextWins) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3);
        SubtitleBoxPresentation tracker;const auto old=tracker.Consume(Resolve({a,b}));
        auto noisy=b;noisy.text.bounds.left-=20;noisy.text.lineBounds[0].left-=20;
        const auto held=tracker.Consume(Resolve({noisy,c}));
        Assert::IsTrue(held.detected && held.held);Assert::AreEqual(old.cue,held.cue);
        auto changed=Pixels();Text(changed,175,307,22,true);Text(changed,240,336,12);
        const auto n3=Measure(changed,3),n4=Measure(changed,4);
        const auto next=tracker.Consume(Resolve({n3,n4}));
        Assert::IsTrue(next.detected && !next.held);Assert::IsTrue(next.cue!=old.cue);
    }
    TEST_METHOD(BlankCueEndCannotBridgeEvenWhenIdenticalTextReturnsNextFrame) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto blank=Pixels();SubtitleBoxPresentation tracker;
        const auto a=Measure(pixels,1),b=Measure(pixels,2),returned=Measure(pixels,3);
        Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({Measure(blank,2),returned})).detected);
        SubtitleBoxPresentation fresh;auto miss=b;miss.text={};
        Assert::IsFalse(fresh.Consume(Resolve({miss,returned})).detected,L"rescue cannot start a cue");
    }
    TEST_METHOD(EstablishedTwoLineGlyphTemplateSurvivesScenePixelsAndOneLineRegrouping) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto accepted=tracker.Consume(Resolve({first,lock}));
        Assert::IsTrue(accepted.detected);Assert::AreEqual(2,accepted.lineCount);
        auto noisy=LowerOnlyCue(3);
        auto changed=std::make_shared<SubtitleInkSnapshot>(*noisy.ink);
        // Current picture texture changes inside the upper line envelope, while
        // both acquired rows remain present in the raw glyph mask.
        const auto& upper=first.text.lineBounds[0];
        for(int y=upper.top;y<upper.bottom;++y)for(int x=upper.left;x<upper.right;++x)
            if((x+y)%40==0 && !changed->Get(x,y)) {
                const size_t p=size_t(y)*changed->width+x;
                changed->rawInk[p/64]|=uint64_t{1}<<(p%64);
            }
        noisy.ink=changed;
        const auto established=tracker.Consume(Resolve({lock,noisy}));
        Assert::IsTrue(established.detected);Assert::AreEqual(2,established.lineCount);
        const auto held=tracker.Consume(Resolve({noisy}));
        Assert::IsTrue(held.detected && held.held);
        Assert::AreEqual(2,held.lineCount);
        Assert::AreEqual(established.cue,held.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(established.bounds,held.bounds,0));
    }
    TEST_METHOD(AlternatingDetectorBoundsCannotFlashAnUnchangedSubtitleByFiftyPixels) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;SubtitleCutPastePresentation placement;
        const auto firstPreview=Resolve({first,lock});
        const auto acquired=tracker.Consume(firstPreview);
        const auto originalMove=placement.Consume(acquired,firstPreview);
        Assert::IsTrue(acquired.detected && originalMove.valid);
        const auto lockPreview=Resolve({lock});
        const auto locked=tracker.Consume(lockPreview);
        const auto lockedMove=placement.Consume(locked,lockPreview);
        Assert::AreEqual(acquired.cue,locked.cue);
        Assert::IsTrue(lockedMove.valid);

        // The OCR-free grouping estimate alternates by 10px between frames,
        // which prevents its short lookahead from confirming any position.
        // The complete current source-pixel glyph mask remains unchanged.
        const int offsets[]={-50,-40,-30,-40};
        for(int i=0;i<3;++i) {
            auto current=Cue(uint64_t(3+i),true);
            auto following=Cue(uint64_t(4+i),true);
            auto shiftGrouping=[](SubtitleBoxObservation& observation,int offset) {
                observation.text.bounds.top+=offset;
                observation.text.bounds.bottom+=offset;
                for(int line=0;line<observation.text.lineCount;++line) {
                    observation.text.lineBounds[line].top+=offset;
                    observation.text.lineBounds[line].bottom+=offset;
                }
            };
            shiftGrouping(current,offsets[i]);
            shiftGrouping(following,offsets[i+1]);
            Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,current),
                L"fixed-coordinate glyphs in the current frame prove the same subtitle");
            Assert::IsFalse(SubtitleBoxLookahead::SameCue(first,current),
                L"the detector's shifting group estimate cannot confirm a stable position");
            const auto preview=Resolve({current,following});
            Assert::AreEqual(1u,preview.matchingFrames);
            const auto result=tracker.Consume(preview);
            const auto moved=placement.Consume(result,preview);
            Assert::IsTrue(result.detected && result.held && moved.valid);
            Assert::AreEqual(acquired.cue,result.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(originalMove.destination,moved.destination,0),
                L"the moved subtitle stays at one destination through alternating 50px grouping errors");
        }
    }
    static void AddWideJoinedWord(SubtitleInkSnapshot& ink,int left,int top,
        int width,int height) {
        for(int y=top;y<top+height;++y) for(int x=left;x<left+width;++x) {
            const bool pixel=(y>=top+height*2/3 && y<top+height*2/3+2) ||
                ((x-left)%18<2 && y>=top+2 && y<top+height-1);
            if(!pixel)continue;
            const size_t p=size_t(y)*ink.width+x;
            ink.rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
    }
    TEST_METHOD(SolidScenePatchCannotImpersonateAnAcquiredGlyphTemplate) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto accepted=tracker.Consume(Resolve({first,lock}));
        auto scene=LowerOnlyCue(3);
        auto changed=std::make_shared<SubtitleInkSnapshot>(*scene.ink);
        const auto& upper=first.text.lineBounds[0];
        for(int y=upper.top;y<upper.bottom;++y)for(int x=upper.left;x<upper.right;++x) {
            const size_t p=size_t(y)*changed->width+x;
            changed->rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
        scene.ink=changed;
        tracker.Consume(Resolve({lock,scene}));
        const auto next=tracker.Consume(Resolve({scene}));
        Assert::IsTrue(next.detected);
        Assert::IsFalse(next.held);
        Assert::IsTrue(next.cue!=accepted.cue,L"a bright block cannot keep the old cue identity");
        Assert::AreEqual(1,next.lineCount);
    }
    TEST_METHOD(ExistingAndSlightlyMovingSceneStrokeOutsideCueDoesNotInvalidateItsMask) {
        auto reference=Cue(1,true),current=Cue(2,true);
        auto oldInk=std::make_shared<SubtitleInkSnapshot>(*reference.ink);
        auto nowInk=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        for(int y=318;y<332;++y) {
            const size_t oldPixel=size_t(y)*oldInk->width+190;
            oldInk->rawInk[oldPixel/64]|=uint64_t{1}<<(oldPixel%64);
            const size_t newPixel=size_t(y+1)*nowInk->width+190;
            nowInk->rawInk[newPixel/64]|=uint64_t{1}<<(newPixel%64);
        }
        reference.ink=oldInk;current.ink=nowInk;
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(reference,current),
            L"a pre-existing railing stroke or one-row scene shift is not new subtitle extent");
    }
    TEST_METHOD(ConfirmedDifferentCueWinsWhenCurrentGlyphMaskChanges) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto accepted=tracker.Consume(Resolve({first,lock}));
        auto changed=Cue(3,true),confirmed=Cue(4,true);
        for(auto* cue:{&changed,&confirmed}) {
            cue->text.lineSignatures[0].fill(0xaa);
            cue->text.lineSignatures[1].fill(0x77);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*cue->ink);
            for(int line=0;line<cue->text.lineCount;++line) {
                const auto& bounds=cue->text.lineBounds[line];
                for(int y=bounds.top;y<bounds.bottom;++y)
                    for(int x=bounds.left;x<bounds.right;++x) {
                        const size_t pixel=size_t(y)*ink->width+x;
                        ink->rawInk[pixel/64]&=~(uint64_t{1}<<(pixel%64));
                    }
                for(int glyph=0;glyph<(bounds.right-bounds.left)/13;++glyph)
                    AddNewGlyph(*ink,bounds.left+glyph*13,bounds.top,true);
            }
            cue->ink=ink;
        }
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(first,changed));
        tracker.Consume(Resolve({lock,changed}));
        const auto next=tracker.Consume(Resolve({changed,confirmed}));
        Assert::IsTrue(next.detected);
        Assert::IsFalse(next.held);
        Assert::IsTrue(next.cue!=accepted.cue,L"confirmed text change takes precedence over the old template");
    }
    TEST_METHOD(ChangedMissingUpperLineCannotHoldTheOldTwoLineCue) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        Assert::IsTrue(acquired.detected && acquired.lineCount==2);
        const auto locked=tracker.Consume(Resolve({lock}));
        Assert::AreEqual(acquired.cue,locked.cue);
        for(uint64_t sequence=3;sequence<=5;++sequence) {
            auto partial=LowerOnlyCue(sequence);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
            const auto& upper=first.text.lineBounds[0];
            for(int y=upper.top;y<upper.bottom;++y)for(int x=upper.left;x<upper.right;++x) {
                const int glyphX=(x-upper.left)%9,glyphY=y-upper.top;
                const bool hShape=glyphX<2 || glyphX>=7 || (glyphY>=6 && glyphY<=7);
                const size_t p=size_t(y)*ink->width+x;
                const uint64_t bit=uint64_t{1}<<(p%64);
                if(hShape)ink->rawInk[p/64]|=bit;else ink->rawInk[p/64]&=~bit;
            }
            partial.ink=ink;
            const auto current=tracker.Consume(Resolve({partial}));
            Assert::IsTrue(current.detected);
            Assert::IsFalse(current.held,L"changed upper strokes cannot be carried by the unchanged lower line");
            Assert::AreEqual(1,current.lineCount);
            Assert::IsTrue(current.cue!=acquired.cue);
        }
    }
    TEST_METHOD(NewGlyphsJustOutsideMissingLineExpandInsteadOfKeepingTheOldNarrowBox) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        const auto locked=tracker.Consume(Resolve({lock}));
        Assert::AreEqual(acquired.cue,locked.cue);
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        for(int glyph=0;glyph<3;++glyph) {
            AddNewGlyph(*ink,130+glyph*13,318);
            AddNewGlyph(*ink,470+glyph*13,318);
        }
        partial.ink=ink;
        SubtitleBoxRect extension;
        Assert::IsTrue(SubtitleBoxLookahead::FindNewInkExtension(
            *first.ink,*ink,first.text.lineBounds[0],first.width,first.height,extension));
        Assert::IsTrue(extension.Valid());
        auto following=partial;
        following.identity=Identity(4);
        auto followingInk=std::make_shared<SubtitleInkSnapshot>(*ink);
        following.ink=followingInk;
        const auto current=tracker.Consume(Resolve({partial,following}));
        Assert::IsTrue(current.detected);
        Assert::AreEqual(acquired.cue,current.cue,L"confirmed new glyphs extend the established cue instead of reacquiring it");
        Assert::AreEqual(2,current.lineCount);
        Assert::IsTrue(current.bounds.left<acquired.bounds.left,
            L"the established box expands to include the persistent glyphs outside its prior bounds");
        Assert::IsTrue(current.bounds.right>acquired.bounds.right,
            L"pixels between both existing line extents do not suppress a right-side extension");
    }
    TEST_METHOD(SingleNarrowSubtitleGlyphCanExtendAfterContiguousConfirmation) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        for(int y=318;y<332;++y) {
            const size_t p=size_t(y)*ink->width+448;
            ink->rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
        partial.ink=ink;
        auto following=partial;following.identity=Identity(4);
        const auto current=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,current.cue);
        Assert::IsTrue(current.bounds.right>acquired.bounds.right,
            L"a persistent one-pixel-wide glyph is retained in the cue bounds");
    }
    TEST_METHOD(WideJoinedArabicWordCanExtendBeyondTheAcquiredLine) {
        auto reference=LowerOnlyCue(1);
        auto current=LowerOnlyCue(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        AddWideJoinedWord(*ink,420,338,60,10);
        current.ink=ink;
        SubtitleBoxRect extension;
        Assert::IsTrue(SubtitleBoxLookahead::FindNewInkExtension(
            *reference.ink,*current.ink,reference.text.lineBounds[0],640,360,extension));
        Assert::IsTrue(extension.left<=420 && extension.right>=480,
            L"a joined word wider than twice the line height remains eligible on the established baseline");
    }
    TEST_METHOD(ExpandedBoxRequiresTheSameAddedGlyphsToRemainVisible) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        AddNewGlyph(*ink,160,318);
        partial.ink=ink;
        auto following=partial;following.identity=Identity(4);
        const auto expanded=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.left<acquired.bounds.left);
        tracker.Consume(Resolve({following}));
        auto vanished=LowerOnlyCue(5),stillGone=LowerOnlyCue(6);
        const auto reacquired=tracker.Consume(Resolve({vanished,stillGone}));
        Assert::IsTrue(reacquired.detected);
        Assert::IsTrue(reacquired.cue!=acquired.cue,
            L"removing the newly acquired glyphs cannot preserve a stale expanded cue box");
        Assert::AreEqual(1,reacquired.lineCount);
    }
    TEST_METHOD(ExpandedCueSurvivesDetectorMissWithEmptyOwnedInk) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        AddNewGlyph(*ink,160,318);
        partial.ink=ink;
        auto following=partial;following.identity=Identity(4);
        const auto expanded=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.left<acquired.bounds.left);
        tracker.Consume(Resolve({following}));

        auto miss=partial;miss.identity=Identity(5);miss.text={};
        auto missedInk=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        missedInk->ownedInk.clear();
        miss.ink=missedInk;
        auto recovered=partial;recovered.identity=Identity(6);
        const auto result=tracker.Consume(Resolve({miss,recovered}));
        Assert::IsTrue(result.detected && result.held,
            L"a raw-ink grouping miss must not crash or lose a proven cue");
        Assert::AreEqual(acquired.cue,result.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(expanded.bounds,result.bounds,0));
    }
    TEST_METHOD(DifferentExtensionGlyphStrokesCannotConfirmTheSameBounds) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto outlined=LowerOnlyCue(3),confirmed=LowerOnlyCue(4);
        auto o=std::make_shared<SubtitleInkSnapshot>(*outlined.ink);
        AddNewGlyph(*o,160,318,false);
        outlined.ink=o;confirmed.ink=std::make_shared<SubtitleInkSnapshot>(*o);
        const auto expanded=tracker.Consume(Resolve({outlined,confirmed}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.left<acquired.bounds.left);
        tracker.Consume(Resolve({confirmed}));
        auto changed=LowerOnlyCue(5),returning=LowerOnlyCue(6);
        auto h=std::make_shared<SubtitleInkSnapshot>(*changed.ink);
        AddNewGlyph(*h,160,318,true);
        changed.ink=h;
        returning.ink=std::make_shared<SubtitleInkSnapshot>(*o);
        const auto result=tracker.Consume(Resolve({changed,returning}));
        Assert::IsTrue(result.detected);
        Assert::IsTrue(result.cue!=acquired.cue,
            L"a changed glyph takes precedence over the old box even if old-shaped pixels return next");
        Assert::AreEqual(1,result.lineCount);
    }
    TEST_METHOD(LaterExpansionInkIsTrackedAndCannotDisappearBehindTheFirstGlyph) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));

        auto initial=LowerOnlyCue(3);
        auto initialInk=std::make_shared<SubtitleInkSnapshot>(*initial.ink);
        AddNewGlyph(*initialInk,448,318);
        initial.ink=initialInk;
        auto initialFollowing=initial;initialFollowing.identity=Identity(4);
        const auto firstExpansion=tracker.Consume(Resolve({initial,initialFollowing}));
        Assert::AreEqual(acquired.cue,firstExpansion.cue);
        Assert::IsTrue(firstExpansion.bounds.right>acquired.bounds.right);
        tracker.Consume(Resolve({initialFollowing}));

        auto later=LowerOnlyCue(5);
        auto laterInk=std::make_shared<SubtitleInkSnapshot>(*later.ink);
        AddNewGlyph(*laterInk,448,318);
        AddNewGlyph(*laterInk,470,318);
        later.ink=laterInk;
        auto laterFollowing=later;laterFollowing.identity=Identity(6);
        const auto secondExpansion=tracker.Consume(Resolve({later,laterFollowing}));
        Assert::AreEqual(acquired.cue,secondExpansion.cue);
        Assert::IsTrue(secondExpansion.bounds.right>firstExpansion.bounds.right,
            L"a second confirmed glyph expands the existing cue box");
        tracker.Consume(Resolve({laterFollowing}));

        auto oneFrameDropout=LowerOnlyCue(7);
        auto oneFrameInk=std::make_shared<SubtitleInkSnapshot>(*oneFrameDropout.ink);
        AddNewGlyph(*oneFrameInk,448,318);
        oneFrameDropout.ink=oneFrameInk;
        auto returned=LowerOnlyCue(8);
        auto returnedInk=std::make_shared<SubtitleInkSnapshot>(*returned.ink);
        AddNewGlyph(*returnedInk,448,318);
        AddNewGlyph(*returnedInk,470,318);
        returned.ink=returnedInk;
        const auto bridged=tracker.Consume(Resolve({oneFrameDropout,returned}));
        Assert::IsTrue(bridged.detected && bridged.held,
            L"a single missing extension glyph bridges only when the exact learned mask returns next");
        Assert::AreEqual(acquired.cue,bridged.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(bridged.bounds,secondExpansion.bounds,0));
        tracker.Consume(Resolve({returned}));

        auto lostSecond=LowerOnlyCue(9);
        auto lostSecondInk=std::make_shared<SubtitleInkSnapshot>(*lostSecond.ink);
        AddNewGlyph(*lostSecondInk,448,318);
        lostSecond.ink=lostSecondInk;
        auto stillLost=lostSecond;stillLost.identity=Identity(10);
        const auto reacquired=tracker.Consume(Resolve({lostSecond,stillLost}));
        Assert::IsTrue(reacquired.detected);
        Assert::AreEqual(1,reacquired.lineCount);
        Assert::IsTrue(reacquired.cue!=acquired.cue,
            L"losing a later extension cannot preserve the stale expanded cue");
        Assert::IsTrue(reacquired.bounds.right<secondExpansion.bounds.right);
    }
    TEST_METHOD(ChangingPictureTextureInsideTheLineDoesNotBecomeExpansionEvidence) {
        auto first=Cue(1,true),lock=Cue(2,true);
        first.pictureBottom=lock.pictureBottom=320;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));

        auto partial=LowerOnlyCue(3);
        partial.pictureBottom=320;
        auto noisy=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        AddNewGlyph(*noisy,448,318);
        // Two new picture-side specks in each original inter-glyph gap. They
        // are tolerated by the acquired-line mask and lie inside its envelope.
        for(int gap=0;gap<26;++gap)for(int y=320;y<322;++y) {
            const int x=205+gap*9;
            const size_t p=size_t(y)*noisy->width+x;
            noisy->rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
        partial.ink=noisy;
        auto following=partial;following.identity=Identity(4);
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,partial));
        const auto expanded=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::AreEqual(2,expanded.lineCount);
        tracker.Consume(Resolve({following}));

        auto clean=LowerOnlyCue(5);
        clean.pictureBottom=320;
        auto cleanInk=std::make_shared<SubtitleInkSnapshot>(*clean.ink);
        AddNewGlyph(*cleanInk,448,318);
        clean.ink=cleanInk;
        auto cleanFollowing=clean;cleanFollowing.identity=Identity(6);
        const auto stillSame=tracker.Consume(Resolve({clean,cleanFollowing}));
        Assert::IsTrue(stillSame.detected);
        Assert::AreEqual(acquired.cue,stillSame.cue,
            L"tolerated picture texture vanishing cannot discard the subtitle extension");
        Assert::AreEqual(2,stillSame.lineCount);
    }
    TEST_METHOD(PermanentlyMissingExtensionGlyphCannotHideInsideGlobalCoverage) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));

        auto expanded=LowerOnlyCue(3);
        auto allGlyphs=std::make_shared<SubtitleInkSnapshot>(*expanded.ink);
        for(int glyph=0;glyph<8;++glyph)AddNewGlyph(*allGlyphs,448+glyph*10,318);
        expanded.ink=allGlyphs;
        auto confirmed=expanded;confirmed.identity=Identity(4);
        const auto wider=tracker.Consume(Resolve({expanded,confirmed}));
        Assert::AreEqual(acquired.cue,wider.cue);
        Assert::IsTrue(wider.bounds.right>acquired.bounds.right);
        tracker.Consume(Resolve({confirmed}));

        auto missing=LowerOnlyCue(5);
        auto sevenGlyphs=std::make_shared<SubtitleInkSnapshot>(*missing.ink);
        for(int glyph=0;glyph<7;++glyph)AddNewGlyph(*sevenGlyphs,448+glyph*10,318);
        missing.ink=sevenGlyphs;
        auto stillMissing=missing;stillMissing.identity=Identity(6);
        const auto reacquired=tracker.Consume(Resolve({missing,stillMissing}));
        Assert::IsTrue(reacquired.detected);
        Assert::AreEqual(1,reacquired.lineCount);
        Assert::IsTrue(reacquired.cue!=acquired.cue,
            L"global coverage by the other seven extensions cannot keep a missing glyph");
        Assert::IsTrue(reacquired.bounds.right<wider.bounds.right);
    }
    TEST_METHOD(VerticalSceneExtensionNeedsFullFixedPixelHorizon) {
        auto acquire=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation transient;
        const auto original=transient.Consume(Resolve({acquire,lock}));
        transient.Consume(Resolve({lock}));
        auto rail=Cue(3,true);
        auto railInk=std::make_shared<SubtitleInkSnapshot>(*rail.ink);
        AddNewGlyph(*railInk,310,292);
        rail.ink=railInk;
        auto near1=rail;near1.identity=Identity(4);
        auto near2=rail;near2.identity=Identity(5);
        auto drift=Cue(6,true);
        const auto rejected=transient.Consume(Resolve({rail,near1,near2,drift}));
        Assert::AreEqual(original.bounds.top,rejected.bounds.top,
            L"a transient vertical scene row cannot enlarge the measured subtitle");

        SubtitleBoxPresentation shortHorizon;
        const auto shortOriginal=shortHorizon.Consume(Resolve({acquire,lock}));
        shortHorizon.Consume(Resolve({lock}));
        const auto held=shortHorizon.Consume(Resolve({rail,near1}));
        Assert::AreEqual(shortOriginal.bounds.top,held.bounds.top,
            L"a short queue retains the proven subtitle instead of accepting a vertical candidate");
    }
    TEST_METHOD(ShiftedGlyphWhollyInsidePictureCannotCountAsCurrentBarText) {
        auto reference=Cue(1,false),current=Cue(2,false);
        reference.pictureBottom=current.pictureBottom=331;
        auto shifted=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        std::fill(shifted->rawInk.begin(),shifted->rawInk.end(),0);
        const auto& line=current.text.lineBounds[0];
        for(int y=line.top;y<line.bottom;++y)for(int x=line.left;x<line.right;++x)
            if(current.ink->Get(x,y)) {
                const int movedY=y-1;
                const size_t p=size_t(movedY)*shifted->width+x;
                shifted->rawInk[p/64]|=uint64_t{1}<<(p%64);
            }
        current.ink=shifted;
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(reference,current),
            L"the actual matched current rows are all above the black-bar boundary");
    }
    TEST_METHOD(SameCueRemainsAnchoredByOneExactCurrentBlackBarRow) {
        auto reference=Cue(1,false),current=Cue(2,false);
        reference.pictureBottom=current.pictureBottom=331;
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(reference,current),
            L"the last exact glyph row at y=331 is still current bar evidence");
    }
    TEST_METHOD(LocalChangedGlyphCannotHideInsideAnOtherwiseUnchangedLongCue) {
        auto original=Pixels();Text(original,175,307,22);Text(original,240,336,12);
        auto changed=original;Text(changed,175+13*10,307,1,true);
        const auto a=Measure(original,1),b=Measure(original,2),later=Measure(original,3);
        auto miss=Measure(changed,2);miss.text={};
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(a,miss));
        SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({miss,later})).detected);
    }
    TEST_METHOD(RemovedUpperLineCannotBorrowFutureInkOrPreserveTheOldFullBox) {
        auto both=Pixels();Text(both,175,307,22);Text(both,240,336,12);
        auto lower=Pixels();Text(lower,240,336,12);
        const auto a=Measure(both,1),b=Measure(both,2),c=Measure(both,3),d=Measure(both,4);
        const auto current=Measure(lower,2);
        const auto preview=Resolve({current,c,d});
        Assert::AreEqual(1,preview.text.lineCount,L"future pixels cannot supply the removed line");
        SubtitleBoxPresentation tracker;const auto old=tracker.Consume(Resolve({a,b}));
        auto miss=current;miss.text={};
        Assert::IsFalse(tracker.Consume(Resolve({miss,c,d})).detected);
        Assert::IsTrue(old.detected);
    }
    TEST_METHOD(CurrentGlyphReuseCannotCrossUnknownBarsOrCurrentSequenceGap) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3),d=Measure(pixels,4);
        for(int kind=0;kind<7;++kind) {
            SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
            auto miss=b,future=c;miss.text={};
            if(kind==0)miss.barAuthority=false;
            if(kind==1)miss.pictureBottom-=2;
            if(kind==2)miss.discontinuity=true;
            if(kind==3)miss.text.workLimit=true;
            if(kind==4)miss.identity.acceptedSequence++;
            if(kind==5){future.text={};future.ink.reset();}
            if(kind==6)future.identity.acceptedSequence++;
            const auto result=tracker.Consume(Resolve({miss,future,d}));
            if(kind<5)Assert::IsFalse(result.detected);
            else Assert::IsTrue(result.detected && result.held,
                L"future gaps or missing future analysis cannot revoke valid current glyphs");
        }
    }
    TEST_METHOD(CurrentGlyphValidationDoesNotRequireFutureGroupingRecovery) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3),d=Measure(pixels,4),e=Measure(pixels,5);
        for(bool noFuture:{false,true}) {
            SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
            auto m2=b,m3=c,m4=d;m2.text={};m3.text={};m4.text={};
            const auto current=tracker.Consume(noFuture?Resolve({m2}):Resolve({m2,m3,m4,e}));
            Assert::IsTrue(current.detected && current.held);
        }
    }
    TEST_METHOD(MovingCurrentInkCannotDriftTheFixedAcquisitionReference) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto shifted=Pixels();Text(shifted,176,307,22);Text(shifted,241,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3);
        auto miss=Measure(shifted,2);miss.text={};
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(a,miss));
        SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({miss,c})).detected);
    }
    TEST_METHOD(SnapshotRowsAndOwnershipMustSupportEveryReferenceLine) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1);auto b=Measure(pixels,2);
        Assert::IsTrue(a.ink && b.ink);Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(a,b));
        auto moved=std::make_shared<SubtitleInkSnapshot>(*b.ink);moved->sourceRows[307]++;
        b.ink=moved;Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(a,b));
        b=a;auto noOwner=std::make_shared<SubtitleInkSnapshot>(*a.ink);noOwner->ownedInk.clear();
        b.ink=noOwner;Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(b,a));
    }
    TEST_METHOD(MissingSnapshotFailsClosedForFutureCompanionsAndBridging) {
        auto first=Cue(1),future=Cue(2,true),later=Cue(3,true);
        first.ink.reset();Assert::AreEqual(1,Resolve({first,future,later}).text.lineCount);
        SubtitleBoxPresentation tracker;const auto initial=tracker.Consume(Resolve({Cue(1),Cue(2)}));
        auto miss=Cue(2);miss.text={};miss.ink.reset();
        Assert::IsTrue(initial.detected);Assert::IsFalse(tracker.Consume(Resolve({miss,Cue(3)})).detected);
    }
    TEST_METHOD(OnePixelMeasuredBarFringeDoesNotInterruptTheSameProvenCue) {
        auto first=Cue(1),second=Cue(2),third=Cue(3),fourth=Cue(4);
        second.pictureBottom--;fourth.pictureTop++;
        SubtitleBoxPresentation tracker;
        const auto a=tracker.Consume(Resolve({first,second,third,fourth}));
        const auto b=tracker.Consume(Resolve({second,third,fourth}));
        const auto c=tracker.Consume(Resolve({third,fourth}));
        const auto d=tracker.Consume(Resolve({fourth}));
        Assert::IsTrue(a.detected && b.detected && c.detected && d.detected);
        Assert::AreEqual(a.cue,d.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(a.bounds,d.bounds,0));
    }
    TEST_METHOD(BarFringeToleranceCannotBridgeMissingBarsOrAnActualBoundaryMove) {
        auto first=Cue(1),next=Cue(2);
        next.pictureBottom-=2;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,next));
        first.pictureTop=1;next=first;next.pictureTop=0;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,next));
        next=first;next.barAuthority=false;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,next));
    }
    TEST_METHOD(ScaleRelativeBoundaryWobbleKeepsCueContextButAspectChangeBreaksIt) {
        auto first=Cue(1),wobble=Cue(2);
        first.width=wobble.width=2560;first.height=wobble.height=1440;
        first.pictureTop=180;first.pictureBottom=1260;
        wobble.pictureTop=187;wobble.pictureBottom=1253;
        Assert::IsTrue(SubtitleBoxLookahead::SameContext(first,wobble),
            L"a seven-pixel boundary wobble at 1440p is within the scale-relative allowance");
        wobble.pictureBottom=1252;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,wobble),
            L"a larger boundary shift breaks continuity and requires reacquisition");
    }
    TEST_METHOD(SubtitleBarsProveAsymmetricAndSingleEdgesIndependently) {
        for(int mode=0;mode<3;++mode) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,64);
            const int top=mode==1?0:35,bottom=mode==2?360:305;
            Fill(pixels,0,top,640,bottom,300);
            const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::IsTrue(evidence.available);Assert::AreEqual(top,evidence.top);
            Assert::AreEqual(bottom,evidence.bottom);
        }
    }
    TEST_METHOD(SubtitleBarsKeepActualBoundaryUnderWideCenteredOpaquePanels) {
        for(int width:{192,352,480}) {
            auto pixels=Pixels();
            const int left=(640-width)/2;
            // The black panel crosses the picture/bar edge. At 55% and 75%
            // width its upper edge would win an all-column run median.
            Fill(pixels,left,280,left+width,340,64);
            Text(pixels,255,301,10);
            const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::IsTrue(evidence.available);
            Assert::AreEqual(45,evidence.top);
            Assert::AreEqual(315,evidence.bottom,
                L"a centered subtitle panel must not become the bar plane");
            Assert::IsTrue(evidence.bottomBoundarySupport>=8);
        }
    }
    TEST_METHOD(SubtitleBarsDoNotInventBoundaryFromCenteredPanelWithoutSideBars) {
        for(int width:{192,352,480}) {
            auto pixels=Pixels(true);
            const int left=(640-width)/2;
            Fill(pixels,left,280,left+width,360,64);
            Assert::IsFalse(ExtractSubtitleBarEvidence(Source(pixels)).available,
                L"an interior panel edge cannot establish a full-width bar");
        }
    }
    TEST_METHOD(SubtitleBarsRejectConnectedPictureOnOnlyTheAffectedEdge) {
        for(bool top:{false,true}) {
            auto pixels=Pixels();
            Fill(pixels,190,top?35:315,450,top?45:325,300);
            const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::IsTrue(evidence.available,L"unaffected bar remains usable");
            Assert::AreEqual(top?0:45,evidence.top);
            Assert::AreEqual(top?315:360,evidence.bottom);
        }
    }
    TEST_METHOD(SubtitleBarsRejectSmoothFullRasterDarkGradients) {
        const int w=1040,h=585;
        for(int mode=0;mode<5;++mode) {
            std::vector<uint8_t> pixels(w*h*4,255);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const int distance=(std::min)(y,h-1-y);
                const int ramp=mode==0?distance/2:mode==1?distance/10:
                    mode==2?distance*2:mode==3?distance*3:(std::max)(0,distance-30)*2;
                const auto level=uint8_t((std::min)(100,ramp));
                auto* pixel=&pixels[(y*w+x)*4];pixel[0]=pixel[1]=pixel[2]=level;
            }
            Assert::IsFalse(ExtractSubtitleBarEvidence(RgbSource(pixels,w,h)).available);
        }
    }
    TEST_METHOD(SubtitleBarsRejectUniformBlackDarkAndFullRasterPicture) {
        for(int level:{0,64,90,300}) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,level);
            Assert::IsFalse(ExtractSubtitleBarEvidence(Source(pixels)).available);
        }
    }
    TEST_METHOD(SubtitleBarsAcceptSparseCrossingGlyphsAndPreserveExactBoundary) {
        auto pixels=Pixels();Text(pixels,180,310,22);
        const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
        Assert::IsTrue(evidence.available);Assert::AreEqual(45,evidence.top);
        Assert::AreEqual(315,evidence.bottom);
        Assert::IsTrue(Measure(pixels,1).text.detected);
        auto above=Pixels();Text(above,180,299,22);
        Assert::IsFalse(Measure(above,2).text.detected,L"glyphs entirely inside picture have no bar anchor");
    }
    TEST_METHOD(SubtitleBarsUseBothSidesOfLearnedBlackFloor) {
        auto pixels=Pixels();
        // Retain the learned64 edge floor but give the alleged bars alternating
        // dark texture below it; one-sided luma<=floor+12 would accept this.
        for(int y=3;y<45;++y)for(int x=0;x<640;++x) {
            const int code=((x/8+y/3)%2)?0:64;
            pixels[y*640+x]=uint16_t(code<<6);
            pixels[(359-y)*640+x]=uint16_t(code<<6);
        }
        Assert::IsFalse(ExtractSubtitleBarEvidence(Source(pixels)).available);
    }
    TEST_METHOD(SubtitleBottomBarIgnoresOneDarkSceneRowWithoutRelaxingIntrusionGate) {
        auto pixels=Pixels();
        // One row of dark picture is accidentally included by the median run.
        // The adjacent shallower edge is independently visible in this frame.
        Fill(pixels,0,314,640,315,64);
        Fill(pixels,380,314,530,315,700);
        const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
        Assert::IsTrue(evidence.available);
        Assert::AreEqual(315,evidence.bottom);
        Assert::IsTrue(evidence.bottomRecoveredAtShallowerEdge);

        auto unsafe=Pixels();
        // A long moving-image intrusion deeper than the two-row recovery window
        // remains vetoed; a merely dark median cannot certify a bar.
        Fill(unsafe,0,310,640,315,64);
        Fill(unsafe,380,310,530,315,700);
        const auto rejected=ExtractSubtitleBarEvidence(Source(unsafe));
        Assert::AreEqual(360,rejected.bottom,
            L"persistent content must leave the bottom edge uncertified");
        Assert::IsFalse(rejected.bottomRecoveredAtShallowerEdge,
            L"shallower recovery must not leap over persistent picture content");
    }
    TEST_METHOD(SubtitleBarsRgbOddHeightAndLimitedRangeLumaHaveExactGeometry) {
        const int w=1040,h=585,top=74,bottom=511;
        std::vector<uint8_t> pixels(w*h*4,255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const auto value=uint8_t(y>=top&&y<bottom?100:0);
            auto* pixel=&pixels[(y*w+x)*4];pixel[0]=pixel[1]=pixel[2]=value;
        }
        const auto evidence=ExtractSubtitleBarEvidence(RgbSource(pixels,w,h));
        Assert::IsTrue(evidence.available);Assert::AreEqual(top,evidence.top);
        Assert::AreEqual(bottom,evidence.bottom);
        auto limited=Pixels();Fill(limited,0,45,640,315,80);
        const auto dim=ExtractSubtitleBarEvidence(Source(limited));
        Assert::IsTrue(dim.available);Assert::AreEqual(45,dim.top);Assert::AreEqual(315,dim.bottom);
    }
    TEST_METHOD(SubtitleReferencePlaneUsesFreshDistributedEdgeWithoutAcquisitionAuthority) {
        auto strong=Pixels();
        const auto reference=ExtractSubtitleBarEvidence(Source(strong));
        Assert::AreEqual(315,reference.bottom);
        auto dark=Pixels();Fill(dark,0,0,640,360,64);
        // Most picture columns blend into black. Sixteen isolated portions of
        // the old encoded edge remain visible across all four horizontal zones.
        for(int i=4;i<128;i+=8) {
            const int x=(640*(4*128+96*(2*i+1)))/(200*128);
            Fill(dark,x,300,x+1,315,90);
        }
        const auto acquired=ExtractSubtitleBarEvidence(Source(dark));
        Assert::IsFalse(acquired.available);
        const auto revisited=RevalidateSubtitleBarEvidence(Source(dark),acquired,reference.top,reference.bottom);
        Assert::IsTrue(revisited.bottomRevalidatedAtReference);
        Assert::IsFalse(revisited.available,L"reference proof must never masquerade as strong acquisition");
        Assert::AreEqual(acquired.bottom,revisited.bottom);
        Assert::AreEqual(acquired.bottomDepth,revisited.bottomDepth);
        Assert::IsTrue(revisited.bottomReferenceBoundarySupport>=8);
        const auto absent=RevalidateSubtitleBarEvidence(Source(dark),acquired,0,360);
        Assert::IsFalse(absent.topRevalidatedAtReference || absent.bottomRevalidatedAtReference);
    }
    TEST_METHOD(SubtitleReferencePlaneRejectsStrongAspectChangesAndUniformFrames) {
        for(int bottom:{300,330,360}) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,64);Fill(pixels,0,45,640,bottom,300);
            const auto raw=ExtractSubtitleBarEvidence(Source(pixels));
            const auto revisited=RevalidateSubtitleBarEvidence(Source(pixels),raw,45,315);
            Assert::IsFalse(revisited.bottomRevalidatedAtReference);
        }
        for(int level:{0,64,90,300}) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,level);
            const auto raw=ExtractSubtitleBarEvidence(Source(pixels));
            const auto revisited=RevalidateSubtitleBarEvidence(Source(pixels),raw,45,315);
            Assert::IsFalse(revisited.topRevalidatedAtReference || revisited.bottomRevalidatedAtReference);
        }
        auto fringe=Pixels();Fill(fringe,0,314,640,315,64);
        const auto raw=ExtractSubtitleBarEvidence(Source(fringe));
        Assert::AreEqual(314,raw.bottom);
        Assert::IsTrue(RevalidateSubtitleBarEvidence(Source(fringe),raw,45,315).bottomRevalidatedAtReference,
            L"a nearby edge can normalize only with fresh proof at the reference plane");
    }
    TEST_METHOD(SubtitleReferencePlaneRejectsSmoothSlopeAndClusteredBoundaryPatch) {
        auto gradient=Pixels();Fill(gradient,0,0,640,360,64);
        for(int y=0;y<360;++y)Fill(gradient,0,y,640,y+1,64+(359-y)/2);
        const auto raw=ExtractSubtitleBarEvidence(Source(gradient));
        Assert::IsFalse(RevalidateSubtitleBarEvidence(Source(gradient),raw,0,315).bottomRevalidatedAtReference);
        auto clustered=Pixels();Fill(clustered,0,0,640,360,64);Fill(clustered,30,300,100,315,90);
        const auto patch=ExtractSubtitleBarEvidence(Source(clustered));
        const auto revisited=RevalidateSubtitleBarEvidence(Source(clustered),patch,0,315);
        Assert::IsTrue(revisited.bottomReferenceBoundarySupport>=8);
        Assert::IsFalse(revisited.bottomRevalidatedAtReference,L"one horizontal zone cannot prove an old plane");
    }
    TEST_METHOD(SubtitleReferencePlaneRejectsConnectedIntrusionEvenWhenAlmostAllBarIsBlack) {
        auto pixels=Pixels();Fill(pixels,190,326,450,330,300);
        const auto raw=ExtractSubtitleBarEvidence(Source(pixels));
        const auto revisited=RevalidateSubtitleBarEvidence(Source(pixels),raw,45,315);
        Assert::IsTrue(revisited.bottomReferenceBlackFraction>0.85);
        Assert::IsTrue(revisited.bottomReferenceMaxIntrusion>=25);
        Assert::IsFalse(revisited.bottomRevalidatedAtReference);
    }
    TEST_METHOD(SubtitleOwnedCrossingStrokesCanExplainOnlyTheirBarSamples) {
        auto pixels=Pixels();
        auto sampleX=[](int i) {return (640*(4*128+96*(2*i+1)))/(200*128);};
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=640;ink->height=360;ink->step=1;ink->sourceRows.resize(360);
        ink->rawInk.resize((640*360+63)/64);ink->ownedInk.resize(ink->rawInk.size());
        for(int y=0;y<360;++y)ink->sourceRows[size_t(y)]=y;
        // Thirty bright samples in one bar row create a false long intrusion
        // unless they are explained by the provisional detector's owned text.
        for(int i=48;i<78;++i) {
            const int x=sampleX(i);Fill(pixels,x,330,x+1,331,700);
            if((i-48)%4==0) {
                const size_t p=size_t(330)*640+x;
                ink->ownedInk[p/64]|=uint64_t{1}<<(p%64);
            }
        }
        const auto source=Source(pixels);
        const auto current=ExtractSubtitleBarEvidence(source);
        const auto ordinary=RevalidateSubtitleBarEvidence(source,current,45,315);
        Assert::IsTrue(ordinary.bottomReferenceMaxIntrusion>=25);
        Assert::IsFalse(ordinary.bottomRevalidatedAtReference,
            L"unexplained bright pixels still invalidate a tracked bar plane");
        const SubtitleBoxRect crossingLine{sampleX(48),300,sampleX(78),340};
        const auto masked=RevalidateSubtitleBarEvidence(source,current,45,315,ink.get(),&crossingLine);
        Assert::IsTrue(masked.bottomRevalidatedAtReference,
            L"only the bounded, detector-owned text samples may be excluded from the bar test");
        Assert::AreEqual(current.available,masked.available,
            L"candidate-masked proof must not create acquisition authority");
        Assert::AreEqual(current.bottom,masked.bottom,
            L"candidate-masked proof must not change the acquired lower edge");
        for(int i=48;i<78;++i) {
            const int x=sampleX(i);const size_t p=size_t(330)*640+x;
            ink->ownedInk[p/64]|=uint64_t{1}<<(p%64);
        }
        const auto broadPatch=RevalidateSubtitleBarEvidence(source,current,45,315,ink.get(),&crossingLine);
        Assert::IsFalse(broadPatch.bottomRevalidatedAtReference,
            L"a broad contiguous patch cannot be excused as subtitle-owned ink");
    }
    TEST_METHOD(SubtitleBarMaskMapsAgainstDetectorSamplingAt1440p) {
        constexpr int width=2560,height=1440,top=184,bottom=1256;
        std::vector<uint8_t> pixels(size_t(width)*height*4,0);
        for(int y=top;y<bottom;++y)for(int x=0;x<width;++x) {
            auto* p=&pixels[(size_t(y)*width+x)*4];p[0]=p[1]=p[2]=100;p[3]=255;
        }
        auto sampleX=[](int i) {return (2560*(4*128+96*(2*i+1)))/(200*128);};
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=(width+2)/3;ink->height=(height+2)/3;ink->step=3;
        ink->sourceRows.resize(size_t(ink->height));
        ink->rawInk.resize((size_t(ink->width)*ink->height+63)/64);
        ink->ownedInk.resize(ink->rawInk.size());
        for(int y=0;y<ink->height;++y)ink->sourceRows[size_t(y)]=y*3;
        for(int i=48;i<78;++i) {
            const int x=sampleX(i);auto* p=&pixels[(size_t(1301)*width+x)*4];
            p[0]=p[1]=p[2]=255;p[3]=255;
            if((i-48)%4==0) {
                const size_t pixel=size_t(1302/3)*ink->width+x/3;
                ink->ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
        }
        const auto source=RgbSource(pixels,width,height);
        const auto current=ExtractSubtitleBarEvidence(source);
        const auto ordinary=RevalidateSubtitleBarEvidence(source,current,top,bottom);
        Assert::IsTrue(ordinary.bottomReferenceMaxIntrusion>=25);
        Assert::IsFalse(ordinary.bottomRevalidatedAtReference);
        const SubtitleBoxRect crossingLine{sampleX(48),1200,sampleX(78),1330};
        const auto masked=RevalidateSubtitleBarEvidence(source,current,top,bottom,ink.get(),&crossingLine);
        Assert::IsTrue(masked.bottomRevalidatedAtReference,
            L"native bar stride and detector mask stride differ at 1440p");
    }
    TEST_METHOD(SubtitleReferencePlaneChecksLastNativeBarRowBetweenAnalysisSamples) {
        const int w=1040,h=1080,top=135,bottom=945;
        std::vector<uint8_t> pixels(size_t(w)*h*4,255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            auto* p=&pixels[(size_t(y)*w+x)*4];
            p[0]=p[1]=p[2]=uint8_t(y>=top&&y<bottom?100:0);
        }
        // Bottom depth135 has an even final sample, so use depth134 where the
        // last native row is odd and would otherwise escape the step2 scan.
        const int referenceBottom=946;
        for(int x=300;x<740;++x) {
            auto* p=&pixels[(size_t(referenceBottom)*w+x)*4];p[0]=p[1]=p[2]=100;
        }
        const auto source=RgbSource(pixels,w,h);
        const auto raw=ExtractSubtitleBarEvidence(source);
        const auto revisited=RevalidateSubtitleBarEvidence(source,raw,top,referenceBottom);
        Assert::IsFalse(revisited.bottomRevalidatedAtReference);
        Assert::IsTrue(revisited.bottomReferenceMaxIntrusion>=25);
    }
    TEST_METHOD(TrackedBarPlaneKeepsOneCueAndBoxThroughMoreThanOneHundredDarkFrames) {
        auto strong=Pixels(),weak=WeakBottomPixels();SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({Measure(strong,1)})).detected);
        SubtitleBoxResult first;
        for(uint64_t sequence=2;sequence<123;++sequence) {
            const auto prior=tracker.BarTrackingReferenceFor(Identity(sequence),640,360);
            Assert::IsTrue(prior.valid);Assert::AreEqual(sequence-1,prior.identity.acceptedSequence);
            Assert::AreEqual(315,prior.pictureBottom);
            const auto current=MeasureWithBar(weak,sequence,prior);
            Assert::AreEqual(360,current.barEvidence.bottom,L"ordinary bottom acquisition remains unavailable");
            Assert::IsTrue(current.barEvidence.bottomRevalidatedAtReference);
            Assert::IsTrue(current.barTrackingAuthority && current.text.detected);
            const auto next=MeasureWithBar(weak,sequence+1,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
            const auto shown=tracker.Consume(Resolve({current,next}));
            Assert::IsTrue(shown.detected);
            if(sequence==2)first=shown;
            else {
                Assert::AreEqual(first.cue,shown.cue);
                Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.bounds,shown.bounds,0));
            }
        }
    }
    TEST_METHOD(WeakBottomStartupCannotAcquireWithoutStrongPrecedingPlane) {
        auto weak=WeakBottomPixels();
        const auto first=Measure(weak,1),second=Measure(weak,2);
        Assert::AreEqual(360,first.pictureBottom);
        Assert::IsFalse(first.barTrackingAuthority || first.text.detected);
        SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({first,second})).detected);
    }
    TEST_METHOD(NewCueAtTrackedBottomRequiresDistinctLookaheadEvenWithStrongTopBar) {
        auto strong=Pixels(),weak=WeakBottomPixels();SubtitleBoxPresentation tracker;
        tracker.Consume(Resolve({Measure(strong,1)}));
        const auto current=MeasureWithBar(weak,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        Assert::IsTrue(current.barAuthority,L"the unrelated top bar is still independently strong");
        Assert::IsTrue(current.barEvidence.bottomRevalidatedAtReference && current.text.detected);
        SubtitleBoxPresentation single;
        Assert::IsFalse(single.Consume(Resolve({current})).detected,
            L"a strong unrelated edge cannot grant acquisition to a weak tracked caption edge");
        SubtitleBoxPresentation repeated;
        Assert::IsFalse(repeated.Consume(Resolve({current,current})).detected);
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        const auto result=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(result.detected);
        Assert::AreEqual(315,tracker.BarTrackingReferenceFor(Identity(3),640,360).pictureBottom);
    }
    TEST_METHOD(SubtitleEntirelyInsideTrackedBarRequiresLookaheadDespiteStrongOppositeBar) {
        auto strong=Pixels(false);SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({Measure(strong,1)})).detected);
        auto weak=WeakBottomPixels(false);Text(weak,240,334,12);
        const auto prior=tracker.BarTrackingReferenceFor(Identity(2),640,360);
        const auto current=MeasureWithBar(weak,2,prior);
        Assert::IsTrue(current.barAuthority && current.barTrackingAuthority);
        Assert::IsTrue(current.text.detected);
        Assert::IsTrue(current.currentAnchorUsesTrackedEdge,
            L"all anchor ink in the tracked bar depends on that boundary, even away from its edge");
        Assert::IsFalse(tracker.Consume(Resolve({current})).detected,
            L"a strong opposite bar cannot authorize a singleton entirely inside the tracked bar");
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        Assert::IsFalse(tracker.Consume(Resolve({current,next})).detected,
            L"replaying an already presented identity cannot add lookahead confirmation");
        SubtitleBoxPresentation confirmed;
        confirmed.Consume(Resolve({Measure(strong,1)}));
        const auto confirmedCurrent=MeasureWithBar(weak,2,
            confirmed.BarTrackingReferenceFor(Identity(2),640,360));
        const auto confirmedNext=MeasureWithBar(weak,3,
            SubtitleBoxLookahead::AdvanceBarTrackingReference(confirmedCurrent));
        Assert::IsTrue(confirmed.Consume(Resolve({confirmedCurrent,confirmedNext})).detected,
            L"two distinct source frames confirm the stable tracked-bar cue");
    }
    TEST_METHOD(CadenceRepeatReusesExactPreviewAndKeepsCutPastePlacement) {
        auto strong=Pixels(false);SubtitleBoxPresentation tracker;
        tracker.Consume(Resolve({Measure(strong,1)}));
        auto weak=WeakBottomPixels();
        const auto current=MeasureWithBar(weak,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        auto firstPreview=Resolve({current,next});
        firstPreview.newScanMs=4.0;firstPreview.newScanFrames=2;
        const auto first=tracker.Consume(firstPreview);
        SubtitleCutPastePresentation cutPaste;
        const auto placed=cutPaste.Consume(first,firstPreview);
        Assert::IsTrue(first.detected && placed.valid);
        Assert::AreEqual(315,placed.pictureBottom);

        const auto rawRepeat=Measure(weak,2);
        auto repeatedPreview=Resolve({rawRepeat});
        Assert::IsTrue(tracker.ReusePreviewForRepeatedSourceFrame(Identity(2),640,360,false,
            repeatedPreview.policyGeneration,repeatedPreview.continuityGeneration,repeatedPreview));
        Assert::IsTrue(repeatedPreview.current.barTrackingAuthority);
        Assert::AreEqual(0.0,repeatedPreview.newScanMs);
        Assert::AreEqual(0u,repeatedPreview.newScanFrames);
        const auto repeated=tracker.Consume(repeatedPreview);
        const auto repeatedPlacement=cutPaste.Consume(repeated,repeatedPreview);
        Assert::AreEqual(first.cue,repeated.cue);
        Assert::IsTrue(repeatedPlacement.valid,
            L"repeating the same pixels reuses their proof instead of losing the tracked plane");
        Assert::AreEqual(315,repeatedPlacement.pictureBottom);
    }
    TEST_METHOD(CachedTrackedMeasurementMustMatchItsIncomingReferenceAndGenerations) {
        constexpr uint64_t policy=11,continuity=21;
        auto strong=Pixels(false);auto first=MeasureWithBar(strong,1,{},policy,continuity);
        const auto firstReference=SubtitleBoxLookahead::AdvanceBarTrackingReference(first);
        auto weak=WeakBottomPixels();
        const auto second=MeasureWithBar(weak,2,firstReference,policy,continuity);
        Assert::IsTrue(second.barTrackingAuthority);
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(second,Identity(2),640,360,
            policy,continuity,false,firstReference));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(second,Identity(2),640,360,
            policy+1,continuity,false,firstReference));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(second,Identity(2),640,360,
            policy,continuity,false,{}));
        const auto secondReference=SubtitleBoxLookahead::AdvanceBarTrackingReference(second);
        const auto third=MeasureWithBar(weak,3,secondReference,policy,continuity);
        Assert::IsTrue(third.barTrackingAuthority);
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(third,Identity(3),640,360,
            policy,continuity,false,firstReference),
            L"a cached frame cannot restore proof whose immediate predecessor was discarded");
    }
    TEST_METHOD(CachedMeasurementMissMustNotSurviveNewIncomingTrackingReference) {
        constexpr uint64_t policy=11,continuity=21;
        auto strong=Pixels(false);auto first=MeasureWithBar(strong,1,{},policy,continuity);
        const auto newlyAvailableReference=SubtitleBoxLookahead::AdvanceBarTrackingReference(first);
        auto weak=WeakBottomPixels();
        const auto oldAttempt=MeasureWithBar(weak,2,{},policy,continuity);
        const auto freshAttempt=MeasureWithBar(weak,2,newlyAvailableReference,policy,continuity);
        Assert::IsTrue(freshAttempt.barTrackingAuthority,
            L"the pixels can be validated when the newly available predecessor is supplied");
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(oldAttempt,Identity(2),640,360,
            policy,continuity,false,newlyAvailableReference),
            L"a cached miss measured without that predecessor must be rescanned");
        const auto wrongPolicy=MeasureWithBar(weak,2,newlyAvailableReference,policy+1,continuity);
        const auto wrongContinuity=MeasureWithBar(weak,2,newlyAvailableReference,policy,continuity+1);
        Assert::IsFalse(wrongPolicy.barTrackingAuthority);
        Assert::IsFalse(wrongContinuity.barTrackingAuthority);
    }
    TEST_METHOD(RepeatedPreviewReuseRejectsChangedSourceAndContext) {
        auto strong=Pixels(false);SubtitleBoxPresentation tracker;
        auto original=Resolve({MeasureWithBar(strong,1,{},7,8)});
        original.newScanMs=3.0;original.newScanFrames=1;
        tracker.Consume(original);
        SubtitleBoxPreview repeated;
        Assert::IsTrue(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,false,
            7,8,repeated));
        Assert::AreEqual(0.0,repeated.newScanMs);Assert::AreEqual(0u,repeated.newScanFrames);
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(2),640,360,false,
            7,8,repeated),L"a different accepted source frame is never treated as cadence reuse");
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),1280,720,false,
            7,8,repeated));
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,true,
            7,8,repeated));
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,false,
            9,8,repeated));
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,false,
            7,9,repeated));
    }
    TEST_METHOD(StrongAspectChangeRejectsOldTrackedPlaneAndAcquiresNewCueGeometry) {
        auto firstPixels=Pixels();Text(firstPixels,240,310,12);
        const auto first=Measure(firstPixels,1);SubtitleBoxPresentation tracker;
        const auto old=tracker.Consume(Resolve({first}));Assert::IsTrue(old.detected);
        auto changed=Pixels();Fill(changed,0,0,640,360,64);Fill(changed,0,60,640,300,300);
        Text(changed,240,294,12);
        const auto current=MeasureWithBar(changed,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        Assert::IsTrue(current.barAuthority);
        Assert::IsFalse(current.barTrackingAuthority);
        Assert::AreEqual(60,current.pictureTop);Assert::AreEqual(300,current.pictureBottom);
        const auto next=MeasureWithBar(changed,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        const auto result=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(result.detected);Assert::AreNotEqual(old.cue,result.cue);
        Assert::AreEqual(300,tracker.BarTrackingReferenceFor(Identity(3),640,360).pictureBottom);
    }
    TEST_METHOD(OneFailedPlaneValidationBreaksTheLowerReferenceChain) {
        auto strong=Pixels(),weak=WeakBottomPixels();SubtitleBoxPresentation tracker;
        tracker.Consume(Resolve({Measure(strong,1)}));
        const auto current=MeasureWithBar(weak,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        Assert::IsTrue(tracker.Consume(Resolve({current,next})).detected);
        auto absent=WeakBottomPixels(false);Fill(absent,0,300,640,360,64);
        const auto failed=MeasureWithBar(absent,3,tracker.BarTrackingReferenceFor(Identity(3),640,360));
        Assert::IsFalse(failed.barEvidence.bottomRevalidatedAtReference);
        Assert::IsFalse(tracker.Consume(Resolve({failed})).detected);
        const auto prior=tracker.BarTrackingReferenceFor(Identity(4),640,360);
        Assert::IsTrue(prior.valid,L"unaffected strong top edge remains usable");
        Assert::AreEqual(360,prior.pictureBottom,L"lost bottom history must not survive through the top edge");
        const auto returned=MeasureWithBar(weak,4,prior);
        Assert::IsFalse(returned.barEvidence.bottomRevalidatedAtReference || returned.text.detected);
        const auto reacquired=MeasureWithBar(strong,5,SubtitleBoxLookahead::AdvanceBarTrackingReference(returned));
        Assert::IsTrue(reacquired.barAuthority);Assert::AreEqual(315,reacquired.pictureBottom);
    }
    TEST_METHOD(SubtitleBarsUhdSamplingWorkIsBounded) {
        const int w=3840,h=2160;std::vector<uint8_t> pixels(size_t(w)*h*4,255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            auto* pixel=&pixels[(size_t(y)*w+x)*4];
            pixel[0]=pixel[1]=pixel[2]=uint8_t(y>=276&&y<1884?100:0);
        }
        const auto evidence=ExtractSubtitleBarEvidence(RgbSource(pixels,w,h));
        Assert::IsTrue(evidence.available);Assert::AreEqual(276,evidence.top);
        Assert::AreEqual(1884,evidence.bottom);Assert::IsTrue(evidence.samples<50000);
    }
    TEST_METHOD(FirstDisplayedFrameIncludesFutureCompanionWithoutChangingCurrentIdentity) {
        SubtitleBoxPresentation tracker;
        const auto preview=Resolve({Cue(1),Cue(2,true),Cue(3,true)});
        const auto result=tracker.Consume(preview);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::AreEqual(352,result.bounds.bottom);Assert::AreEqual(uint64_t(1),preview.current.identity.acceptedSequence);
        Assert::AreEqual(3u,preview.matchingFrames);
    }
    TEST_METHOD(CrowdedThirdRowCannotPoisonTwoStableBarAnchoredRows) {
        auto crowded=[](uint64_t sequence,int sceneShift) {
            SubtitleBoxObservation o=Cue(sequence,true);
            o.pictureBottom=340;o.text.lineCount=3;
            o.text.lineBounds[2]={280+sceneShift,280,360+sceneShift,296};
            o.text.lineSignatures[2].fill(0x77);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*o.ink);
            for(int y=280;y<296;++y)for(int x=280+sceneShift;x<360+sceneShift;++x)
                if((x-(280+sceneShift))%8<4) {
                    const size_t p=size_t(y)*640+x;
                    const uint64_t bit=uint64_t{1}<<(p%64);
                    ink->rawInk[p/64]|=bit;ink->ownedInk[p/64]|=bit;
                }
            o.ink=ink;
            return o;
        };
        const auto steady=Resolve({crowded(10,0),crowded(11,0),crowded(12,0),crowded(13,0)});
        Assert::IsTrue(steady.currentLinesConfirmed);
        Assert::AreEqual(3,steady.text.lineCount,L"a stable genuine third row remains eligible");
        const auto preview=Resolve({crowded(1,0),crowded(2,0),crowded(3,0),crowded(4,3)});
        Assert::AreEqual(4u,preview.matchingFrames);
        Assert::IsTrue(preview.currentLinesConfirmed);
        Assert::AreEqual(2,preview.text.lineCount);
        Assert::AreEqual(318,preview.text.bounds.top);
        Assert::AreEqual(348,preview.text.bounds.bottom);
    }
    TEST_METHOD(TranslucentBackingTopEdgeIsAuxiliaryEvidenceOnly) {
        auto blank=Pixels();
        SubtitleBoxDetector scanner;
        Assert::IsFalse(scanner.Analyze(Source(blank),45,315,1,1).detected);
        auto backed=blank;
        Fill(backed,150,280,500,315,190);
        Text(backed,175,307,22);Text(backed,240,336,12);
        scanner.Reset();
        const auto result=scanner.Analyze(Source(backed),45,315,2,1);
        Assert::IsTrue(result.detected);
        bool found=false;
        for(int line=0;line<result.lineCount;++line)
            if(result.panelTopEdges[line].Valid() &&
                std::abs(result.panelTopEdges[line].y-280)<=2 &&
                result.panelTopEdges[line].supportBasisPoints>=9000)found=true;
        Assert::IsTrue(found,L"detector-owned line has broad backing edge on sampled grid");
        auto narrow=blank;
        Fill(narrow,220,280,250,315,190);
        Text(narrow,175,307,22);Text(narrow,240,336,12);
        scanner.Reset();
        const auto narrowResult=scanner.Analyze(Source(narrow),45,315,3,1);
        Assert::IsTrue(narrowResult.detected);
        for(int line=0;line<narrowResult.lineCount;++line)
            Assert::IsFalse(narrowResult.panelTopEdges[line].Valid(),
                L"a narrow picture edge cannot masquerade as broad subtitle backing");
    }
    TEST_METHOD(StablePanelCanResolveBorderlineCrowdedRowButNotMissingGlyphs) {
        auto crowded=[](uint64_t sequence,bool panel,int lostEvery) {
            auto o=Cue(sequence,true);o.pictureBottom=340;o.text.lineCount=3;
            const SubtitleBoxRect pictureRow={280,280,360,296};
            o.text.lineBounds[2]=pictureRow;o.text.lineSignatures[2].fill(0x77);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*o.ink);
            unsigned ordinal=0;
            for(int y=pictureRow.top;y<pictureRow.bottom;++y)
                for(int x=pictureRow.left;x<pictureRow.right;++x)
                    if((x-pictureRow.left)%8<4) {
                        const size_t p=size_t(y)*640+x;
                        const uint64_t bit=uint64_t{1}<<(p%64);
                        if(!lostEvery || (++ordinal%lostEvery)!=0)
                            ink->rawInk[p/64]|=bit;
                        ink->ownedInk[p/64]|=bit;
                    }
            o.ink=ink;
            if(panel)o.text.panelTopEdges[2]={270,280,360,8000};
            return o;
        };
        const auto noPanel=Resolve({crowded(1,false,0),crowded(2,false,0),
            crowded(3,false,0),crowded(4,false,13)});
        Assert::AreEqual(2,noPanel.text.lineCount,
            L"borderline picture ink alone is insufficient for the farthest row");
        const auto backed=Resolve({crowded(1,true,0),crowded(2,true,0),
            crowded(3,true,0),crowded(4,true,13)});
        Assert::AreEqual(3,backed.text.lineCount,
            L"stable backing supports a row with independently persistent glyphs");
        const auto panelGone=Resolve({crowded(1,true,0),crowded(2,true,0),
            crowded(3,true,0),crowded(4,false,13)});
        Assert::AreEqual(2,panelGone.text.lineCount);
        const auto glyphsGone=Resolve({crowded(1,true,0),crowded(2,true,0),
            crowded(3,true,0),crowded(4,true,4)});
        Assert::AreEqual(2,glyphsGone.text.lineCount,
            L"stable backing cannot replace current/future glyph proof");
    }
    TEST_METHOD(FutureOnlyThirdRowNeedsCurrentAndFarthestFixedPixelProof) {
        auto withPictureRow=[](uint64_t sequence,int shift,bool grouped) {
            SubtitleBoxObservation o=Cue(sequence,true);o.pictureBottom=340;
            auto ink=std::make_shared<SubtitleInkSnapshot>(*o.ink);
            const SubtitleBoxRect row={280+shift,280,360+shift,296};
            for(int y=row.top;y<row.bottom;++y)for(int x=row.left;x<row.right;++x)
                if((x-row.left)%8<4) {
                    const size_t p=size_t(y)*640+x;const uint64_t bit=uint64_t{1}<<(p%64);
                    ink->rawInk[p/64]|=bit;
                    if(grouped)ink->ownedInk[p/64]|=bit;
                }
            o.ink=ink;
            if(grouped) {
                o.text.lineCount=3;o.text.lineBounds[2]=row;
                o.text.lineSignatures[2].fill(0x77);
                o.text.bounds.top=(std::min)(o.text.bounds.top,row.top);
            }
            return o;
        };
        const auto transient=Resolve({withPictureRow(1,0,false),withPictureRow(2,0,true),
            withPictureRow(3,0,true),withPictureRow(4,3,true)});
        Assert::AreEqual(2,transient.text.lineCount);
        Assert::AreEqual(314,transient.text.bounds.top,
            L"future duplicate grouping cannot bloat the current two-line bounds");
        const auto currentInk=Resolve({withPictureRow(10,0,false),withPictureRow(11,0,true),
            withPictureRow(12,0,true),withPictureRow(13,0,true)});
        Assert::AreEqual(3,currentInk.text.lineCount,
            L"a genuine third row already present in current pixels may be added with full-window proof");
        Assert::AreEqual(280,currentInk.text.bounds.top);
    }
    TEST_METHOD(BlankCurrentFrameCannotBorrowFutureCue) {
        auto blank=Cue(1);blank.text={};blank.ink.reset();SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({blank,Cue(2),Cue(3)})).detected);
    }
    TEST_METHOD(OneFrameFlashIsRejectedButLastFrameOfEstablishedCueRemains) {
        auto blank=Cue(2);blank.text={};blank.ink.reset();SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({Cue(1),blank})).detected);
        tracker.Reset();Assert::IsTrue(tracker.Consume(Resolve({Cue(1),Cue(2)})).detected);
        blank.identity=Identity(3);
        Assert::IsTrue(tracker.Consume(Resolve({Cue(2),blank})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({blank,Cue(4)})).detected);
    }
    TEST_METHOD(ChangedUpperLineCannotBorrowUnchangedLowerLine) {
        auto first=Cue(1,true),next=Cue(2,true);
        next.text.lineSignatures[0].fill(0xAA);
        Assert::AreEqual(1u,Resolve({first,next}).matchingFrames);
        SubtitleBoxPresentation tracker;Assert::IsFalse(tracker.Consume(Resolve({first,next})).detected);
    }
    TEST_METHOD(AnchorOrderCanChangeWhenSecondLineAppears) {
        auto second=Cue(2,true);
        std::swap(second.text.lineBounds[0],second.text.lineBounds[1]);
        std::swap(second.text.lineSignatures[0],second.text.lineSignatures[1]);
        Assert::AreEqual(2u,Resolve({Cue(1),second}).matchingFrames);
    }
    TEST_METHOD(OneFutureCompanionCannotEnlargePersistentBarCue) {
        const auto transient=Resolve({Cue(1),Cue(2,true),Cue(3),Cue(4)});
        Assert::AreEqual(1,transient.text.lineCount);Assert::AreEqual(336,transient.text.bounds.bottom);
        const auto confirmed=Resolve({Cue(1),Cue(2,true),Cue(3,true),Cue(4)});
        Assert::AreEqual(2,confirmed.text.lineCount);Assert::AreEqual(352,confirmed.text.bounds.bottom);
    }
    TEST_METHOD(UnconfirmedCurrentCompanionCannotAcquireOrEnlargeCue) {
        const auto transient=Resolve({Cue(1,true),Cue(2),Cue(3)});
        Assert::IsFalse(transient.currentLinesConfirmed);
        SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(transient).detected);
        Assert::IsTrue(tracker.Consume(Resolve({Cue(2),Cue(3)})).detected);
        const auto result=tracker.Consume(Resolve({Cue(3,true),Cue(4),Cue(5)}));
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);Assert::AreEqual(336,result.bounds.bottom);
        auto otherTransient=Cue(4,true);otherTransient.text.lineSignatures[1].fill(0xAA);
        const auto another=tracker.Consume(Resolve({otherTransient,Cue(5),Cue(6)}));
        Assert::IsTrue(another.detected);Assert::AreEqual(1,another.lineCount);Assert::AreEqual(result.cue,another.cue);
        const auto clear=tracker.Consume(Resolve({Cue(5),Cue(6)}));
        Assert::IsTrue(clear.detected);Assert::AreEqual(result.cue,clear.cue);
    }
    TEST_METHOD(GeometryFullRasterSequenceAndGenerationChangesBreakProof) {
        for(int kind=0;kind<8;++kind) {
            auto next=Cue(2);
            if(kind==0)next.pictureTop=43;
            if(kind==1)next.barAuthority=false;
            if(kind==2)next.identity.acceptedSequence=3;
            if(kind==3)next.identity.sourceFormatGeneration++;
            if(kind==4)next.identity.viewportGeneration++;
            if(kind==5)next.discontinuity=true;
            if(kind==6)next.identity.transportGeneration++;
            if(kind==7)next.identity.rendererGeneration++;
            Assert::AreEqual(1u,Resolve({Cue(1),next,Cue(3)}).matchingFrames);
        }
    }
    TEST_METHOD(ProofCannotCrossBlankThenResumeSameCue) {
        auto blank=Cue(2);blank.text={};blank.ink.reset();
        Assert::AreEqual(1u,Resolve({Cue(1),blank,Cue(3)}).matchingFrames);
    }
    TEST_METHOD(ExactIdentityPolicyAndContinuityAreRequiredAtConsumption) {
        const auto preview=Resolve({Cue(1),Cue(2)});
        Assert::IsTrue(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),7,8));
        for(int kind=0;kind<7;++kind) {
            auto identity=Identity(1);
            if(kind==0)identity.transportGeneration++;if(kind==1)identity.acceptedSequence++;
            if(kind==2)identity.sourceFrameNumber++;if(kind==3)identity.captureTimestamp++;
            if(kind==4)identity.sourceFormatGeneration++;if(kind==5)identity.viewportGeneration++;
            if(kind==6)identity.rendererGeneration++;
            Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,identity,7,8));
        }
        Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),9,8));
        Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),7,9));
    }
    TEST_METHOD(RepeatDoesNotAddConfirmationOrChangeAlreadyPresentedBounds) {
        SubtitleBoxPresentation tracker;
        const auto first=tracker.Consume(Resolve({Cue(1)}));
        const auto repeated=tracker.Consume(Resolve({Cue(1),Cue(2,true)}));
        Assert::AreEqual(first.bounds.bottom,repeated.bounds.bottom);
        Assert::AreEqual(first.observations,repeated.observations);
        Assert::AreEqual(first.cue,repeated.cue);
    }
    TEST_METHOD(ZeroFutureFramesUsesImmediateCurrentDetection) {
        SubtitleBoxPresentation tracker;const auto result=tracker.Consume(Resolve({Cue(1)}));
        Assert::IsTrue(result.detected);Assert::AreEqual(1u,result.observations);
    }
    TEST_METHOD(DriftingSegmentationBoundsCannotMoveUnchangedGlyphPixels) {
        SubtitleBoxPresentation tracker;SubtitleBoxResult acquired;
        for(int shift=0;shift<20;++shift) {
            auto cue=Cue(shift+1);cue.text.bounds.left+=shift;cue.text.bounds.right+=shift;
            cue.text.lineBounds[0].left+=shift;cue.text.lineBounds[0].right+=shift;
            const auto result=tracker.Consume(Resolve({cue}));
            if(!shift)acquired=result;
            Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
        }
    }
    TEST_METHOD(SlowDriftCannotAccumulateAnUnboundedCueBox) {
        SubtitleBoxPresentation tracker;uint64_t firstCue=0,lastCue=0;
        for(int shift=0;shift<20;++shift) {
            auto cue=Cue(shift+1);cue.text.bounds.left+=shift;cue.text.bounds.right+=shift;
            cue.text.lineBounds[0].left+=shift;cue.text.lineBounds[0].right+=shift;
            auto shifted=std::make_shared<SubtitleInkSnapshot>(*cue.ink);
            std::fill(shifted->rawInk.begin(),shifted->rawInk.end(),0);
            std::fill(shifted->ownedInk.begin(),shifted->ownedInk.end(),0);
            for(int y=0;y<shifted->height;++y)for(int x=shift;x<shifted->width;++x) {
                const size_t pixel=size_t(y)*shifted->width+x;
                if(cue.ink->Get(x-shift,y))shifted->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
                if(cue.ink->Get(x-shift,y,true))shifted->ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
            cue.ink=shifted;
            const auto result=tracker.Consume(Resolve({cue}));
            if(!shift)firstCue=result.cue;lastCue=result.cue;
            Assert::AreEqual(248,result.bounds.right-result.bounds.left);
        }
        Assert::IsTrue(firstCue!=lastCue);
    }
    TEST_METHOD(FrozenBoundsMustContainCurrentGlyphsEvenWithinIdentityTolerance) {
        SubtitleBoxPresentation tracker;auto cue=Cue(1);cue.height=2160;
        cue.text.bounds=cue.text.lineBounds[0];
        const auto first=tracker.Consume(Resolve({cue}));
        cue.identity=Identity(2);cue.text.lineBounds[0].left+=4;cue.text.lineBounds[0].right+=4;
        cue.text.bounds=cue.text.lineBounds[0];
        const auto shifted=tracker.Consume(Resolve({cue}));
        Assert::IsTrue(shifted.cue!=first.cue);Assert::IsTrue(shifted.bounds.right>=cue.text.lineBounds[0].right);
    }
    TEST_METHOD(UnconfirmedCompanionCannotHideEscapeOfAnEstablishedLine) {
        SubtitleBoxPresentation tracker;auto first=Cue(1);first.height=2160;
        first.text.bounds=first.text.lineBounds[0];
        Assert::IsTrue(tracker.Consume(Resolve({first})).detected);
        auto shifted=Cue(2,true);shifted.height=2160;
        shifted.text.lineBounds[0].left+=4;shifted.text.lineBounds[0].right+=4;
        auto future=Cue(3);future.height=2160;future.text.lineBounds[0]=shifted.text.lineBounds[0];
        const auto result=tracker.Consume(Resolve({shifted,future}));
        Assert::IsFalse(result.detected,L"cannot retain old box after known glyphs escape it");
    }
    TEST_METHOD(RealPixelsRecoverContaminatedBarsAndBothBoundaryCrossingLines) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto first=Measure(pixels,1),second=Measure(pixels,2);
        Assert::IsTrue(first.barAuthority);Assert::IsTrue(first.text.detected);Assert::AreEqual(2,first.text.lineCount);
        SubtitleBoxPresentation tracker;const auto result=tracker.Consume(Resolve({first,second}));
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.top<=307);Assert::IsTrue(result.bounds.bottom>=350);
        auto expanded=Pixels(true);Text(expanded,175,307,22);Text(expanded,240,336,12);
        const auto full=Measure(expanded,3);
        Assert::IsFalse(full.barAuthority);Assert::IsFalse(tracker.Consume(Resolve({full})).detected);
    }
    TEST_METHOD(RealPixelsDetectEqualBoundsChangedTextDespiteUnchangedCompanion) {
        auto first=Pixels(),second=Pixels();
        Text(first,175,307,22);Text(first,240,336,12);
        Text(second,175,307,22,true);Text(second,240,336,12);
        const auto a=Measure(first,1),b=Measure(second,2);
        Assert::IsTrue(a.barAuthority,L"original cue has fresh bars");
        Assert::IsTrue(b.barAuthority,L"changed cue has fresh bars");
        Assert::IsTrue(a.text.detected,L"original cue detected");
        Assert::IsTrue(b.text.detected,L"changed cue detected");
        Assert::AreEqual(2,a.text.lineCount,L"original cue lines");
        Assert::AreEqual(2,b.text.lineCount,L"changed cue lines");
        Assert::IsFalse(SubtitleBoxLookahead::SameCue(a,b));
        Assert::AreEqual(1u,Resolve({a,b}).matchingFrames);
    }
    TEST_METHOD(RealPixelsAspectChangesUseTheCurrentBoundaryInBothDirections) {
        auto narrow=Pixels();Fill(narrow,0,0,640,360,64);Fill(narrow,0,60,640,300,300);
        Text(narrow,200,300,18);
        auto wide=Pixels();Text(wide,200,300,18);
        const auto n1=Measure(narrow,1),n2=Measure(narrow,2);
        Assert::IsTrue(n1.barAuthority&&n1.text.detected);
        SubtitleBoxPresentation tracker;const auto first=tracker.Consume(Resolve({n1,n2}));
        Assert::IsTrue(first.detected);
        const auto w3=Measure(wide,3),w4=Measure(wide,4);
        Assert::IsTrue(w3.barAuthority);Assert::IsFalse(w3.text.detected);
        Assert::IsFalse(tracker.Consume(Resolve({w3,w4})).detected);
        const auto n5=Measure(narrow,5),n6=Measure(narrow,6);
        const auto reacquired=tracker.Consume(Resolve({n5,n6}));
        Assert::IsTrue(reacquired.detected);Assert::IsTrue(first.cue!=reacquired.cue);
        Assert::AreEqual(0u,Resolve({w4,n5,n6}).matchingFrames);
        auto stableNarrow=Pixels();Fill(stableNarrow,0,0,640,360,64);Fill(stableNarrow,0,60,640,300,300);
        auto stableWide=Pixels();Text(stableNarrow,200,336,18);Text(stableWide,200,336,18);
        const auto sw=Measure(stableWide,10),sn=Measure(stableNarrow,11);
        const std::wstring geometry = L"wide="+std::to_wstring(sw.pictureTop)+L","+
            std::to_wstring(sw.pictureBottom)+L" detected="+std::to_wstring(sw.text.detected)+
            L" narrow="+std::to_wstring(sn.pictureTop)+L","+std::to_wstring(sn.pictureBottom)+
            L" detected="+std::to_wstring(sn.text.detected);
        Logger::WriteMessage(geometry.c_str());
        Assert::IsTrue(sw.barAuthority,L"wide lower-bar cue authority");
        Assert::IsTrue(sn.barAuthority,L"narrow lower-bar cue authority");
        Assert::IsTrue(sw.text.detected,L"wide lower-bar cue detected");
        Assert::IsTrue(sn.text.detected,L"narrow lower-bar cue detected");
        Assert::AreEqual(1u,Resolve({sw,sn}).matchingFrames);
    }
    TEST_METHOD(PolicyContinuityAndSourceDiscontinuityResetEstablishedCue) {
        for(int kind=0;kind<3;++kind) {
            SubtitleBoxPresentation tracker;const auto first=tracker.Consume(Resolve({Cue(1),Cue(2)}));
            auto preview=Resolve({Cue(2),Cue(3)});
            if(kind==0)preview.policyGeneration++;if(kind==1)preview.continuityGeneration++;
            if(kind==2)preview.current.discontinuity=true;
            const auto next=tracker.Consume(preview);
            Assert::IsTrue(next.detected);Assert::IsTrue(next.cue!=first.cue);Assert::AreEqual(1u,next.observations);
        }
    }
    TEST_METHOD(AlignedRejectedCaptionEdgeIsOnlyRelaxedForDiagnosticWithFreshPixelProof) {
        auto pixels=Pixels();Text(pixels,200,336,18);
        const auto source=Source(pixels);const auto raw=ExtractActivePictureEvidence(source);
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
        Assert::IsTrue(EvaluateSymmetricVerticalBarHypothesis(source,raw).classification==
            ActivePictureClassification::PROVISIONAL,L"default crop acquisition unchanged");
        const auto diagnostic=EvaluateSymmetricVerticalBarHypothesis(source,raw,true);
        Assert::IsTrue(diagnostic.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
        Assert::IsTrue(diagnostic.trustedBounds.top>=42&&diagnostic.trustedBounds.bottom<=318);
        auto expanded=Pixels();Fill(expanded,0,315,640,330,300);Text(expanded,200,336,18);
        const auto expandedSource=Source(expanded);const auto expandedRaw=ExtractActivePictureEvidence(expandedSource);
        const auto rejected=EvaluateSymmetricVerticalBarHypothesis(expandedSource,expandedRaw,true);
        Assert::IsTrue(rejected.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
            rejected.trustedBounds.bottom>=330,L"connected picture must not become an inferred subtitle bar");
    }
};
}
