#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBoxLookahead.h>
#include <SubtitleCutPaste.h>
#include <SubtitleCueContinuation.h>
#include <SubtitleMeasurementWorker.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <sstream>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleHandoffTests) {
    static constexpr int Width=640,Height=360;
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {2,sequence,sequence,sequence*41708,2,3,4};
    }
    static void Fill(std::vector<uint16_t>& p,int left,int top,int right,int bottom,int code) {
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)p[size_t(y)*Width+x]=uint16_t(code<<6);
    }
    static std::vector<uint16_t> Pixels(int glyphs,bool p210=false,bool top=false,int left=240,int y=310,bool changed=false,int pitch=13) {
        std::vector<uint16_t> p(size_t(Width)*Height*(p210?2:3)/ (p210?1:2),uint16_t(512<<6));
        Fill(p,0,0,Width,Height,64);Fill(p,0,45,Width,315,300);
        if(glyphs) {
            Fill(p,left-4,y-4,left+pitch*(glyphs-1)+12,y+18,64);
            for(int n=0;n<glyphs;++n) {
                const int x=left+pitch*n;
                if(!changed) {
                    Fill(p,x,y,x+8,y+14,700);Fill(p,x+2,y+2,x+6,y+12,64);
                } else {
                    Fill(p,x,y,x+2,y+14,700);Fill(p,x+6,y,x+8,y+14,700);
                    Fill(p,x+2,y+6,x+6,y+8,700);
                }
            }
        }
        if(top) {
            const std::vector<uint16_t> original(p.begin(),p.begin()+Width*Height);
            for(int yy=0;yy<Height;++yy)std::copy_n(original.begin()+size_t(Height-1-yy)*Width,Width,p.begin()+size_t(yy)*Width);
        }
        return p;
    }
    static AnalysisLumaSource Source(std::vector<uint16_t>& p,bool p210=false) {
        AnalysisLumaSource s;s.data=reinterpret_cast<const uint8_t*>(p.data());s.dataBytes=p.size()*2;
        s.width=Width;s.height=Height;s.rowBytes=s.chromaRowBytes=Width*2;
        s.format=p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010;s.generation=2;return s;
    }
    static SubtitleBoxObservation Measure(std::vector<uint16_t>& p,uint64_t seq,bool p210=false) {
        auto s=Source(p,p210);SubtitleBoxDetector detector;
        return SubtitleBoxLookahead::Measure(detector,s,ExtractActivePictureEvidence(s),Identity(seq),false,{},7,8);
    }
    static std::vector<uint16_t> UhdPixels(int glyphs,bool p210) {
        constexpr int w=3840,h=2160;
        std::vector<uint16_t> p(size_t(w)*h*(p210?2:3)/(p210?1:2),uint16_t(512<<6));
        const auto fill=[&](int l,int t,int r,int b,int code) {
            for(int y=t;y<b;++y)for(int x=l;x<r;++x)p[size_t(y)*w+x]=uint16_t(code<<6);
        };
        fill(0,0,w,h,64);fill(0,274,w,1886,300);
        const int left=1440,top=1858,pitch=78;
        fill(left-24,top-24,left+pitch*(glyphs-1)+72,top+108,64);
        for(int n=0;n<glyphs;++n) {
            const int x=left+pitch*n;
            fill(x,top,x+48,top+84,700);fill(x+12,top+12,x+36,top+72,64);
        }
        return p;
    }
    static AnalysisLumaSource UhdSource(std::vector<uint16_t>& p,bool p210) {
        AnalysisLumaSource s;s.data=reinterpret_cast<const uint8_t*>(p.data());s.dataBytes=p.size()*2;
        s.width=3840;s.height=2160;s.rowBytes=s.chromaRowBytes=7680;
        s.format=p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010;s.generation=2;return s;
    }
    static SubtitleBoxObservation MeasureUhd(std::vector<uint16_t>& p,uint64_t seq,bool p210) {
        auto source=UhdSource(p,p210);SubtitleBoxDetector detector;
        return SubtitleBoxLookahead::Measure(detector,source,ExtractActivePictureEvidence(source),Identity(seq),false,{},7,8);
    }
    static SubtitleBoxPreview Resolve(std::initializer_list<SubtitleBoxObservation> observations) {
        return SubtitleBoxLookahead::Resolve(observations.begin(),observations.size(),7,8);
    }
    static bool Same(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }
    static void Bit(std::vector<uint64_t>& bits,size_t index,bool value) {
        const uint64_t mask=uint64_t(1)<<(index%64);
        if(value)bits[index/64]|=mask;else bits[index/64]&=~mask;
    }
    static SubtitleBoxPreview Awaiting(const SubtitleBoxObservation& current) {
        auto next=current;next.identity.acceptedSequence++;
        next.identity.sourceFrameNumber++;next.identity.captureTimestamp+=41708;
        next.text={};next.ink.reset();
        // A ready next source frame with no caption is not confirmation of
        // the current replacement. Keep the real Resolve result unmodified.
        return Resolve({current,next});
    }
    static SubtitleBoxResult Acquire(SubtitleBoxPresentation& tracker,const SubtitleBoxObservation& current) {
        auto second=current;second.identity.acceptedSequence++;
        second.identity.sourceFrameNumber++;second.identity.captureTimestamp+=41708;
        const auto first=tracker.Consume(Resolve({current,second}));
        Assert::IsTrue(first.detected);
        // Source three will contain the contraction; never speculate old
        // pixels for that same immutable identity while establishing source two.
        const auto established=tracker.Consume(Resolve({second}));
        Assert::IsTrue(established.detected && established.observations>=2);
        return established;
    }
    static void AssertCurrentOwnership(const SubtitleBoxResult& result,const SubtitleBoxResult& current) {
        Assert::AreEqual(current.lineCount,result.lineCount);
        for(int i=0;i<current.lineCount;++i) {
            Assert::IsTrue(Same(current.lineBounds[i],result.lineBounds[i]));
            Assert::IsTrue(Same(current.linePanels[i],result.linePanels[i]));
            Assert::IsTrue(Same(current.lineCapturePanels[i],result.lineCapturePanels[i]));
            Assert::IsTrue(current.lineSignatures[i]==result.lineSignatures[i]);
        }
        Assert::IsTrue(current.signature==result.signature && current.anchorSignature==result.anchorSignature);
        Assert::IsTrue(Same(current.anchor,result.anchor));
        Assert::IsTrue(Same(current.capturePanel,result.capturePanel));
        Assert::IsTrue(Same(current.sourcePanel,result.sourcePanel));
        Assert::IsTrue(Same(current.provenSourcePanel,result.provenSourcePanel));
        Assert::IsTrue(current.capturePanelMeasured==result.capturePanelMeasured);
    }
    static bool FindIndependentNativeGap(const SubtitleBoxObservation& old,const SubtitleBoxObservation& current,
        const AnalysisLumaSource& source,int& selectedX,int& selectedRow) {
        const auto& mask=*current.ink;const auto& line=current.text.lineBounds[0];
        const int first=int(std::lower_bound(mask.sourceRows.begin(),mask.sourceRows.end(),line.top)-mask.sourceRows.begin());
        const int last=int(std::lower_bound(mask.sourceRows.begin(),mask.sourceRows.end(),line.bottom)-mask.sourceRows.begin());
        for(int x=line.left/mask.step+2;x<(line.right-2)/mask.step;++x) {
            bool ownedColumn=false;
            for(int row=first;row<last;++row)for(int dx=-1;dx<=1;++dx)
                ownedColumn|=old.ink->Get(x+dx,row,true) || mask.Get(x+dx,row,true);
            if(ownedColumn)continue;
            for(int row=first+2;row<last-2;++row) {
                if(old.ink->Get(x,row) || mask.Get(x,row))continue;
                AnalysisLumaSample pixel;
                if(!source.Sample(x*mask.step,mask.sourceRows[size_t(row)],pixel) || pixel.luma>mask.blackLimit)continue;
                selectedX=x;selectedRow=row;return true;
            }
        }
        return false;
    }
    static void AssertNoProvisionalRoute(const char* group,int fault,
        const SubtitleBoxLookahead::CurrentOwnedContractionProof& proof,
        const SubtitleBoxPresentation& tracker,const SubtitleBoxResult& result) {
        std::ostringstream diagnostic;
        diagnostic<<group<<" case="<<fault<<" proof="<<proof.reason
            <<" decision="<<tracker.DecisionReason()<<" handoff="<<tracker.HandoffReason()
            <<" sources="<<tracker.HandoffSources()<<" detected="<<result.detected
            <<" held="<<result.held<<" pending="<<result.confirmationPending;
        const auto message=diagnostic.str();Logger::WriteMessage(message.c_str());
        const std::wstring wide(message.begin(),message.end());
        Assert::IsFalse(proof.valid,wide.c_str());
        Assert::IsFalse(result.confirmationPending,wide.c_str());
        Assert::AreEqual(unsigned(0),tracker.HandoffSources(),wide.c_str());
    }
public:
    TEST_METHOD(CurrentOwnedSuffixContractionHasNativeProofDespiteOldComponentFailure) {
        for(bool p210:{false,true})for(bool top:{false,true}) {
            auto before=Pixels(12,p210,top),after=Pixels(8,p210,top);
            const auto old=Measure(before,1,p210),now=Measure(after,3,p210);
            Assert::IsTrue(old.text.detected && now.text.detected && old.independentBarAnalysis && now.independentBarAnalysis);
            Assert::IsTrue(old.barEvidence.available && now.barEvidence.available);
            Assert::AreEqual(1,old.text.lineCount);Assert::AreEqual(1,now.text.lineCount);
            Assert::IsTrue(now.text.lineBounds[0].right<old.text.lineBounds[0].right);
            SubtitleBoxLookahead::InkMatchDiagnostics d;
            Assert::IsFalse(SubtitleBoxLookahead::ConfidentSameInk(old,now,&d),
                L"removed suffix components must still fail the original glyph identity check");
            Assert::AreEqual(std::string("component-coverage"),std::string(d.reason));
            const auto proof=SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now);
            Assert::IsTrue(proof.valid);Assert::IsTrue(proof.ownedSamples>0);
            Assert::IsTrue(proof.inspectedSamples>=proof.ownedSamples && proof.inspectedSamples<=65536);
        }
    }
    TEST_METHOD(OneConsumedSourceRequiresPriorConfirmedLookaheadMetadata) {
        auto before=Pixels(12),after=Pixels(8),nextPixels=Pixels(6);
        const auto old=Measure(before,1),current=Measure(after,2),next=Measure(nextPixels,3);
        for(bool declaredConfirmedLookahead:{false,true}) {
            auto initial=Resolve({old});
            if(declaredConfirmedLookahead) {
                // Isolated consumer-policy unit: treat a producer's already
                // validated two-source confirmation as explicit metadata.
                // These flags are intentionally manufactured, not claimed to
                // be natural Resolve output for the differently sized cues.
                initial.matchingFrames=2;initial.currentLinesConfirmed=true;
            }
            SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(initial);
            Assert::IsTrue(acquired.detected && acquired.observations==1);
            Assert::IsTrue(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,current).valid);
            const auto result=tracker.Consume(Resolve({current,next}));
            std::ostringstream diagnostic;diagnostic<<"one consumed source confirmed-metadata="<<declaredConfirmedLookahead
                <<" decision="<<tracker.DecisionReason()<<" handoff="<<tracker.HandoffReason();
            Logger::WriteMessage(diagnostic.str().c_str());
            Assert::IsTrue(result.confirmationPending==declaredConfirmedLookahead);
            Assert::IsTrue(result.detected==declaredConfirmedLookahead);
            Assert::AreEqual(declaredConfirmedLookahead?unsigned(1):unsigned(0),tracker.HandoffSources());
        }
    }
    TEST_METHOD(RealQueuedConfirmationAndItsUnchangedSourceAdvanceBeforeContraction) {
        auto before=Pixels(12),after=Pixels(8),nextPixels=Pixels(6);
        const auto first=Measure(before,1),second=Measure(before,2),current=Measure(after,3),next=Measure(nextPixels,4);
        Assert::IsTrue(first.ink!=second.ink);
        const auto initial=Resolve({first,second});
        Assert::IsTrue(initial.currentLinesConfirmed && initial.matchingFrames==2);
        SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(initial);
        Assert::IsTrue(acquired.detected && acquired.observations==1);
        // The already measured queued source two still has exactly its old
        // native pixels when consumed. No ready source identity is rewritten.
        const auto advanced=tracker.Consume(Resolve({second}));
        Assert::IsTrue(advanced.detected && advanced.observations==2);
        const auto result=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(result.detected && result.confirmationPending);
        Assert::AreEqual(acquired.cue,result.cue);
    }
    TEST_METHOD(NaturalMeasuredQueueBoundaryReachesHandoffWithoutPreviewOverrides) {
        auto before=Pixels(12),after=Pixels(8),blank=Pixels(0);
        const auto old=Measure(before,1),current=Measure(after,3),future=Measure(blank,4);
        SubtitleBoxPresentation tracker;const auto accepted=Acquire(tracker,old);
        const auto preview=Resolve({current,future});
        Assert::IsTrue(preview.available && preview.futureAvailable && preview.current.text.detected);
        Assert::AreEqual(unsigned(1),preview.matchingFrames);
        Assert::IsFalse(preview.currentLinesConfirmed);
        Assert::IsFalse(SubtitleBoxLookahead::ConfidentSameInk(old,current));
        const auto result=tracker.Consume(preview);
        Assert::IsTrue(result.detected && result.confirmationPending);
        Assert::AreEqual(accepted.cue,result.cue);AssertCurrentOwnership(result,current.text);
    }
    TEST_METHOD(UhdCurrentOwnedContractionUsesActualNonAlignedNativeRowMap) {
        for(bool p210:{false,true}) {
            auto before=UhdPixels(12,p210),after=UhdPixels(8,p210);
            const auto old=MeasureUhd(before,1,p210),current=MeasureUhd(after,3,p210);
            Assert::IsTrue(old.text.detected && current.text.detected);
            Assert::AreEqual(4,old.ink->step);Assert::AreEqual(4,current.ink->step);
            Assert::AreEqual(274,old.pictureTop);Assert::AreEqual(1886,old.pictureBottom);
            bool nonuniform=false;
            for(size_t row=0;row<current.ink->sourceRows.size();++row)
                nonuniform|=current.ink->sourceRows[row]!=int(row)*current.ink->step;
            Assert::IsTrue(nonuniform,L"the real UHD fixture must exercise native edge rows between nominal samples");
            SubtitleBoxLookahead::InkMatchDiagnostics d;
            Assert::IsFalse(SubtitleBoxLookahead::ConfidentSameInk(old,current,&d));
            const auto proof=SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,current);
            const std::string detail=std::string("UHD contraction reason=")+proof.reason+" owned="+std::to_string(proof.ownedSamples)+" inspected="+std::to_string(proof.inspectedSamples);
            Logger::WriteMessage(detail.c_str());Assert::IsTrue(proof.valid);
            Assert::IsTrue(proof.inspectedSamples<=65536 && proof.ownedSamples>0);
            SubtitleBoxPresentation tracker;const auto accepted=Acquire(tracker,old);
            const auto result=tracker.Consume(Awaiting(current));
            Assert::IsTrue(result.detected && result.confirmationPending);
            Assert::AreEqual(accepted.cue,result.cue);Assert::IsTrue(Same(accepted.bounds,result.bounds));
            AssertCurrentOwnership(result,current.text);
        }
    }
    TEST_METHOD(EstablishedContractionUsesCurrentPixelsWithoutOneFramePresentationGap) {
        for(bool p210:{false,true})for(bool top:{false,true}) {
            auto before=Pixels(12,p210,top),after=Pixels(8,p210,top);
            const auto old=Measure(before,1,p210),now=Measure(after,3,p210);
            SubtitleBoxPresentation tracker;const auto acquired=Acquire(tracker,old);
            Assert::IsTrue(acquired.detected && !acquired.confirmationPending);
            auto confirmedPixels=Pixels(6,p210,top);
            const auto following=Measure(confirmedPixels,4,p210),confirmed=Measure(confirmedPixels,5,p210);
            const auto preview=Resolve({now,following});const auto pending=tracker.Consume(preview,250,1000.0/23.976);
            Assert::IsTrue(pending.detected && pending.confirmationPending && !pending.held);
            Assert::AreEqual(uint32_t(0),pending.observations);Assert::AreEqual(acquired.cue,pending.cue);
            Assert::IsTrue(Same(acquired.bounds,pending.bounds));AssertCurrentOwnership(pending,now.text);
            Assert::IsFalse(tracker.AcceptedCueSeedCertificate().Matches(preview,pending));
            Assert::IsFalse(tracker.HasAcceptedSourceTemplate());
            const auto accepted=tracker.Consume(Resolve({following,confirmed}),250,1000.0/23.976);
            Assert::IsTrue(accepted.detected && !accepted.confirmationPending);
            Assert::IsTrue(accepted.cue!=acquired.cue);
            AssertCurrentOwnership(accepted,following.text);
        }
    }
    TEST_METHOD(AvailableUnrelatedFutureDoesNotBlankSafeEstablishedCurrentContraction) {
        auto before=Pixels(12),after=Pixels(8);
        const auto old=Measure(before,1),now=Measure(after,3);
        SubtitleBoxPresentation immediate,queued;Acquire(immediate,old);Acquire(queued,old);
        const auto noFuture=immediate.Consume(Resolve({now}));
        const auto waiting=queued.Consume(Awaiting(now));
        Assert::IsTrue(noFuture.detected && waiting.detected);
        Assert::IsFalse(noFuture.confirmationPending);Assert::IsTrue(waiting.confirmationPending);
        AssertCurrentOwnership(noFuture,now.text);AssertCurrentOwnership(waiting,now.text);
    }
    TEST_METHOD(TwoDistinctSourceLimitAndDurationLimitDoNotCountRepeatedOutput) {
        for(double frameMs:{1000.0/23.976,1000.0/60.0})for(bool durationLimited:{false,true}) {
            auto before=Pixels(12),after=Pixels(8),shorter=Pixels(6),last=Pixels(4),ending=Pixels(2);
            const auto old=Measure(before,1),current=Measure(after,3),next=Measure(shorter,4),final=Measure(last,5),end=Measure(ending,6);
            SubtitleBoxPresentation tracker;Acquire(tracker,old);
            const int hold=durationLimited?int(std::floor(frameMs*1.5)):250;
            const auto firstPreview=Resolve({current,next});
            const auto first=tracker.Consume(firstPreview,hold,frameMs);
            Assert::IsTrue(first.detected && first.confirmationPending);
            for(int n=0;n<12;++n) {
                const auto repeat=tracker.Consume(firstPreview,hold,frameMs);
                Assert::IsTrue(repeat.detected && repeat.confirmationPending);
                Assert::AreEqual(first.cue,repeat.cue);Assert::IsTrue(Same(first.bounds,repeat.bounds));
                Assert::AreEqual(unsigned(1),tracker.HandoffSources());
            }
            const auto second=tracker.Consume(Resolve({next,final}),hold,frameMs);
            Assert::IsTrue(second.detected==!durationLimited);
            if(!durationLimited)Assert::IsTrue(second.confirmationPending);
            Assert::IsFalse(tracker.Consume(Resolve({final,end}),hold,frameMs).detected,
                L"unconfirmed current contractions cannot manufacture an indefinitely renewable caption");
        }
    }
    TEST_METHOD(LongEstablishedCueKeepsOriginalOwnershipButUsesRecentStrongSourceAllowance) {
        auto before=Pixels(12),after=Pixels(8),following=Pixels(6);
        const auto old=Measure(before,1);SubtitleBoxPresentation tracker;
        const auto first=Acquire(tracker,old);
        for(uint64_t sequence=3;sequence<=20;++sequence) {
            const auto strong=tracker.Consume(Resolve({Measure(before,sequence)}));
            Assert::IsTrue(strong.detected && !strong.held && !strong.confirmationPending);
            Assert::AreEqual(first.cue,strong.cue);
        }
        const auto current=Measure(after,21),next=Measure(following,22);
        Assert::IsFalse(SubtitleBoxLookahead::ConfidentSameInk(old,current));
        const auto pending=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(pending.detected && pending.confirmationPending,
            L"the immutable acquisition mask must not expire while fresh complete source frames keep validating the cue");
        Assert::AreEqual(first.cue,pending.cue);AssertCurrentOwnership(pending,current.text);
    }
    TEST_METHOD(AdjacentAcceptedSequenceWithPhysicalCaptureGapCannotAuthorizeHandoff) {
        auto before=Pixels(12),after=Pixels(8),following=Pixels(6);
        const auto old=Measure(before,1);SubtitleBoxPresentation tracker;Acquire(tracker,old);
        auto current=Measure(after,3),next=Measure(following,4);
        current.identity.sourceFrameNumber=10;current.identity.captureTimestamp=10*41708;
        next.identity.sourceFrameNumber=11;next.identity.captureTimestamp=11*41708;
        const auto preview=Resolve({current,next});
        Assert::IsTrue(preview.futureAvailable && preview.matchingFrames==1);
        Assert::IsFalse(tracker.Consume(preview).detected,
            L"analysis sequence adjacency does not conceal skipped physical capture frames");
    }
    TEST_METHOD(DisabledOrSubFrameHoldCannotAuthorizeHandoff) {
        for(double frameMs:{1000.0/23.976,1000.0/60.0})for(int hold:{0,1}) {
            auto before=Pixels(12),after=Pixels(8);SubtitleBoxPresentation tracker;Acquire(tracker,Measure(before,1));
            Assert::IsFalse(tracker.Consume(Awaiting(Measure(after,3)),hold,frameMs).detected);
        }
    }
    TEST_METHOD(BlankCurrentFrameCannotBorrowContractionOrFutureReturn) {
        for(bool p210:{false,true}) {
            auto before=Pixels(12,p210),blank=Pixels(0,p210),after=Pixels(8,p210);
            SubtitleBoxPresentation tracker;Acquire(tracker,Measure(before,1,p210));
            const auto gone=Measure(blank,3,p210),next=Measure(after,4,p210);
            Assert::IsFalse(gone.text.detected);
            Assert::IsFalse(tracker.Consume(Resolve({gone,next})).detected);
        }
    }
    TEST_METHOD(NewOrShiftedGlyphPixelsCannotBorrowOriginalCueOwnership) {
        auto before=Pixels(12);const auto old=Measure(before,1);
        for(int kind:{0,1,2,3,4}) {
            auto p=kind==0?Pixels(8,false,false,240,310,true):
                kind==1?Pixels(8,false,false,241):
                kind==2?Pixels(13):kind==3?Pixels(8,false,false,240,309):Pixels(8,false,false,266);
            const auto now=Measure(p,3);
            Assert::IsTrue(now.text.detected);
            const auto proof=SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now);
            SubtitleBoxPresentation tracker;const auto accepted=Acquire(tracker,old);
            const auto result=tracker.Consume(Awaiting(now));
            AssertNoProvisionalRoute("new-or-shifted",kind,proof,tracker,result);
            if(kind==2 && result.detected) {
                Assert::AreEqual(accepted.cue,result.cue);
                Assert::IsTrue(Same(accepted.bounds,result.bounds));
                Assert::IsTrue(result.lineBounds[0].right<=old.text.lineBounds[0].right &&
                    result.lineBounds[0].right<now.text.lineBounds[0].right,
                    L"legacy retention of the original twelve glyphs cannot promote the new thirteenth glyph");
            }
        }
    }
    TEST_METHOD(ExtraCurrentRowOrBisectedGlyphCannotUseSuffixOnlyProof) {
        auto before=Pixels(12),after=Pixels(8);const auto old=Measure(before,1);
        auto row=Pixels(6,false,false,280,338);
        for(int y=334;y<356;++y)for(int x=276;x<365;++x)after[size_t(y)*Width+x]=row[size_t(y)*Width+x];
        const auto extra=Measure(after,3);
        Assert::IsTrue(extra.text.detected && extra.text.lineCount==2);
        Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,extra).valid);
        SubtitleBoxPresentation tracker;Acquire(tracker,old);
        Assert::IsFalse(tracker.Consume(Awaiting(extra)).detected);
        // Truncating inside the last complete retained O leaves some original
        // owned pixels on either side of the new ROI boundary. It is neither
        // a complete removed suffix component nor a complete current glyph.
        auto same=Measure(before,2);
        same.text.lineBounds[0].right-=4;same.text.bounds.right-=4;
        Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,same).valid);
    }
    TEST_METHOD(OriginalRawSceneInkIsNotOriginalOwnedGlyphAuthority) {
        auto before=Pixels(12),after=Pixels(8);
        auto old=Measure(before,1);auto now=Measure(after,3);
        auto oldInk=std::make_shared<SubtitleInkSnapshot>(*old.ink);
        auto nowInk=std::make_shared<SubtitleInkSnapshot>(*now.ink);
        // The hole of the first O was never owned. Making it raw-bright in the
        // original snapshot cannot authorize a new current stroke there.
        const int x=243,y=315;const size_t bit=size_t(y)*oldInk->width+x;
        Assert::IsFalse(oldInk->Get(x,y,true));
        Bit(oldInk->rawInk,bit,true);Bit(nowInk->rawInk,bit,true);Bit(nowInk->ownedInk,bit,true);
        old.ink=oldInk;now.ink=nowInk;
        Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now).valid);
        SubtitleBoxPresentation tracker;Acquire(tracker,old);
        Assert::IsFalse(tracker.Consume(Awaiting(now)).detected);
    }
    TEST_METHOD(ExplicitSnapshotNovelRawUnownedGapRejectsContraction) {
        auto before=Pixels(12,false,false,240,310,false,20),after=Pixels(8,false,false,240,310,false,20);
        const auto old=Measure(before,1);auto current=Measure(after,3);
        Assert::IsTrue(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,current).valid);
        int x=-1,row=-1;
        Assert::IsTrue(FindIndependentNativeGap(old,current,Source(after),x,row));
        // This is deliberately a policy snapshot unit fixture. Native detection
        // is allowed to own an added punctuation dot, so do not mislabel this
        // constructed RAW=true/OWNED=false state as a detector output.
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        const size_t bit=size_t(row)*ink->width+x;
        Assert::IsFalse(old.ink->Get(x,row) || ink->Get(x,row) || ink->Get(x,row,true));
        Bit(ink->rawInk,bit,true);Bit(ink->blackBacking,bit,false);current.ink=ink;
        Assert::IsTrue(current.ink->Get(x,row) && !current.ink->Get(x,row,true));
        const auto proof=SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,current);
        Assert::IsFalse(proof.valid);
        Assert::AreEqual(std::string("contraction-retained-raw-ink-changed"),std::string(proof.reason));
        SubtitleBoxPresentation tracker;Acquire(tracker,old);
        Assert::IsFalse(tracker.Consume(Awaiting(current)).detected);
    }
    TEST_METHOD(ActualNativeGapHighlightCannotUseTheContractionException) {
        auto before=Pixels(12,false,false,240,310,false,20),after=Pixels(8,false,false,240,310,false,20);
        const auto old=Measure(before,1),baseline=Measure(after,3);
        int x=-1,row=-1;
        Assert::IsTrue(FindIndependentNativeGap(old,baseline,Source(after),x,row));
        const int nativeX=x*baseline.ink->step,nativeY=baseline.ink->sourceRows[size_t(row)];
        Fill(after,nativeX,nativeY,nativeX+1,nativeY+1,700);
        const auto current=Measure(after,3);
        const auto proof=SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,current);
        std::ostringstream diagnostic;diagnostic<<"actual native gap x="<<nativeX<<" y="<<nativeY
            <<" detected="<<current.text.detected<<" raw="<<(current.ink && current.ink->Get(x,row))
            <<" owned="<<(current.ink && current.ink->Get(x,row,true))<<" reason="<<proof.reason
            <<" row="<<current.text.lineBounds[0].left<<','<<current.text.lineBounds[0].top<<','
            <<current.text.lineBounds[0].right<<','<<current.text.lineBounds[0].bottom;
        Logger::WriteMessage(diagnostic.str().c_str());
        Assert::IsFalse(proof.valid,
            L"native punctuation ownership, changed grouping, or raw evidence must prevent borrowing the old glyph mask");
        SubtitleBoxPresentation tracker;Acquire(tracker,old);
        const auto result=tracker.Consume(Awaiting(current));
        AssertNoProvisionalRoute("actual-native-gap",0,proof,tracker,result);
    }
    TEST_METHOD(ContractionProofRejectsMissingIncompleteAndOversizedEvidence) {
        auto before=Pixels(12),after=Pixels(8);
        const auto old=Measure(before,1),current=Measure(after,3);
        Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,current,0).valid);
        Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,current,4).valid);
        for(int fault=0;fault<12;++fault) {
            auto now=current;auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
            if(fault==0)now.ink.reset();
            if(fault==1)ink->rawEvidenceComplete=false;
            if(fault==2)ink->paletteValid=false;
            if(fault==3)ink->ownedInk.clear();
            if(fault==4)ink->rawInk.clear();
            if(fault==5)ink->sourceRows.pop_back();
            if(fault==6)now.text.bounds.right=Width+1;
            if(fault==7)now.text.lineBounds[0].right=Width+1;
            if(fault==8) {ink->width=0x7fffffff;ink->height=0x7fffffff;}
            if(fault==9)ink->sourceRows.front()=-1;
            if(fault==10)ink->sourceRows.back()=Height;
            if(fault==11)ink->sourceRows[1]=ink->sourceRows[0];
            if(fault!=0)now.ink=ink;
            Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now).valid);
        }
    }
    TEST_METHOD(RemovedSuffixMustBeRawInkFreeBeforeItsOldWidthCleanupCanBeReused) {
        auto before=Pixels(12),after=Pixels(8);const auto old=Measure(before,1);
        for(bool stroke:{false,true}) {
            auto now=Measure(after,3);auto ink=std::make_shared<SubtitleInkSnapshot>(*now.ink);
            const int width=stroke?2:1,height=stroke?6:1;
            for(int y=315;y<315+height;++y)for(int x=370;x<370+width;++x) {
                const size_t bit=size_t(y)*ink->width+x;
                Assert::IsFalse(ink->Get(x,y,true));
                Bit(ink->rawInk,bit,true);Bit(ink->blackBacking,bit,false);
            }
            now.ink=ink;
            // Black scenery can conceal a card boundary. A fresh cleanup
            // nomination may still span the original width even though the
            // accepted current line no longer captures this surviving ink.
            now.text.sourcePanel=old.text.sourcePanel;
            now.text.provenSourcePanel=old.text.provenSourcePanel;
            now.text.capturePanel=old.text.capturePanel;
            now.text.linePanels=old.text.linePanels;
            now.text.lineCapturePanels=old.text.lineCapturePanels;
            Assert::IsTrue(now.text.lineBounds[0].right<370 && old.text.lineBounds[0].right>371);
            Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now).valid);
            SubtitleBoxPresentation tracker;Acquire(tracker,old);
            Assert::IsFalse(tracker.Consume(Awaiting(now)).detected,
                L"a provisional prefix cannot erase current uncaptured raw ink in the supposedly removed suffix");
        }
    }
    TEST_METHOD(CurrentOwnedPrefixCannotAuthorizeNewCleanupOrCaptureOutsideAcquiredFootprint) {
        auto before=Pixels(12),after=Pixels(8);
        const auto old=Measure(before,1),current=Measure(after,3);
        for(int fault=0;fault<5;++fault) {
            auto now=current;
            if(fault==0)now.text.sourcePanel.right=old.text.sourcePanel.right+1;
            if(fault==1)now.text.provenSourcePanel={old.text.sourcePanel.left,old.text.sourcePanel.top,
                old.text.sourcePanel.right+1,old.text.sourcePanel.bottom};
            if(fault==2)now.text.capturePanel.right=old.text.capturePanel.right+1;
            if(fault==3)now.text.linePanels[0].right=old.text.linePanels[0].right+1;
            if(fault==4)now.text.lineCapturePanels[0].right=old.text.lineCapturePanels[0].right+1;
            Assert::IsFalse(SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now).valid);
            SubtitleBoxPresentation tracker;Acquire(tracker,old);
            Assert::IsFalse(tracker.Consume(Awaiting(now)).detected,
                L"temporary glyph contraction does not authorize expanded source cleanup or glyph capture");
        }
    }
    TEST_METHOD(CurrentPhysicalBarsAndFreshCompleteAnalysisAreMandatoryForProvisionalRoute) {
        auto before=Pixels(12),after=Pixels(8);
        const auto old=Measure(before,1),current=Measure(after,3);
        for(int fault=0;fault<15;++fault) {
            auto now=current;
            if(fault==0)now.independentBarAnalysis=false;
            if(fault==1)now.barAuthority=false;
            if(fault==2)now.barTrackingAuthority=true;
            if(fault==3)now.currentAnchorUsesTrackedEdge=true;
            if(fault==4)now.barEvidence.available=false;
            if(fault==5)now.barEvidence.bottomRevalidatedAtReference=true;
            if(fault==6)now.barEvidence.captionBackedBottom=true;
            if(fault==7)now.glyphLocalBottom=true;
            if(fault==8)now.pendingRefresh=true;
            if(fault==9)now.analysisRefresh=true;
            if(fault==10)now.text.workLimit=true;
            if(fault==11)now.text.held=true;
            if(fault==12)now.analyzed=false;
            if(fault==13)now.pictureBottom--;
            if(fault==14)now.text.diagnosticPictureGlyphProofRequired=true;
            const auto proof=SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now);
            SubtitleBoxPresentation tracker;Acquire(tracker,old);
            const auto result=tracker.Consume(Awaiting(now));
            AssertNoProvisionalRoute("current-physical-bars",fault,proof,tracker,result);
        }
    }
    TEST_METHOD(StaleSequenceContextAndPolicyCannotUseEstablishedHandoff) {
        auto before=Pixels(12),after=Pixels(8);
        const auto old=Measure(before,10),current=Measure(after,12);
        for(int fault=0;fault<12;++fault) {
            auto now=current;
            if(fault==0)now.identity=Identity(9);
            if(fault==1)now.identity=Identity(13);
            if(fault==2)now.identity.transportGeneration++;
            if(fault==3)now.identity.sourceFormatGeneration++;
            if(fault==4)now.identity.analysisDomainKey++;
            if(fault==5)now.identity.viewportGeneration++;
            if(fault==6)now.identity.rendererGeneration++;
            if(fault==7)now.policyGeneration++;
            if(fault==8)now.continuityGeneration++;
            if(fault==9)now.scriptHint=SubtitleScriptHint::Arabic;
            if(fault==10)now.discontinuity=true;
            if(fault==11)now.optimizationMode++;
            const auto proof=SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now);
            SubtitleBoxPresentation tracker;Acquire(tracker,old);
            const auto result=tracker.Consume(Awaiting(now));
            // Accepted-sequence gaps are a tracker boundary, while the pure
            // ownership helper compares immutable original/current pixels.
            if(fault==0 || fault==1) {
                std::ostringstream diagnostic;diagnostic<<"source-sequence case="<<fault
                    <<" decision="<<tracker.DecisionReason()<<" handoff="<<tracker.HandoffReason();
                Logger::WriteMessage(diagnostic.str().c_str());
                Assert::IsFalse(result.confirmationPending);Assert::AreEqual(unsigned(0),tracker.HandoffSources());
            } else AssertNoProvisionalRoute("source-context",fault,proof,tracker,result);
        }
    }
    TEST_METHOD(ContractionAloneCannotAcquireNewCueOrOneFrameStarfield) {
        auto before=Pixels(12),after=Pixels(8);const auto now=Measure(after,3);
        SubtitleBoxPresentation empty;
        Assert::IsFalse(empty.Consume(Awaiting(now)).detected);
        auto old=Measure(before,1);auto ambiguous=now;
        ambiguous.text.diagnosticPictureGlyphProofRequired=true;
        ambiguous.text.diagnosticCoreGlyphCount=14;
        SubtitleBoxPresentation tracker;Acquire(tracker,old);
        Assert::IsFalse(tracker.Consume(Awaiting(ambiguous)).detected,
            L"the picture-only transient component acquisition guard cannot be bypassed by a nearby established slot");
    }
    TEST_METHOD(PendingHandoffCannotSeedMissingEdgeLeaseOrRenewAcceptedTemplate) {
        auto before=Pixels(12),after=Pixels(8);const auto old=Measure(before,1);
        auto shorter=Pixels(6),last=Pixels(4),end=Pixels(2);
        const auto current=Measure(after,3),next=Measure(shorter,4),following=Measure(last,5),ending=Measure(end,6);
        SubtitleBoxPresentation tracker;Acquire(tracker,old);
        auto preview=Resolve({current,next});const auto pending=tracker.Consume(preview);
        Assert::IsTrue(pending.confirmationPending);
        Assert::IsFalse(tracker.AcceptedCueSeedCertificate().Matches(preview,pending));
            Assert::IsFalse(tracker.HasAcceptedSourceTemplate());
        const ActivePictureBounds base{0,45,Width,315,Width,Height};
        preview.current.sharedPicture.required=true;preview.current.sharedPicture.bounds=base;
        preview.current.sharedPicture.identity=preview.current.identity;
        SubtitleCueContinuationGuard guard;
        Assert::IsFalse(guard.Remember(preview,pending,base,Source(after)));
        // A second contraction is still compared against the untouched original
        // acquisition mask; pending output is never its own acquired template.
        const auto second=tracker.Consume(Resolve({next,following}));
        Assert::IsTrue(second.detected && second.confirmationPending);
        Assert::IsFalse(tracker.Consume(Resolve({following,ending})).detected);
    }
    TEST_METHOD(PendingHandoffCannotRefreshPreviouslySeededWorkerOrMissingEdgeGuard) {
        auto before=Pixels(12),after=Pixels(8),following=Pixels(6);
        const auto old=Measure(before,1);SubtitleBoxPresentation tracker;
        const auto accepted=Acquire(tracker,old);
        auto strong=Resolve({Measure(before,2)});
        const ActivePictureBounds base{0,45,Width,315,Width,Height};
        strong.current.sharedPicture.required=true;strong.current.sharedPicture.bounds=base;
        strong.current.sharedPicture.identity=strong.current.identity;
        SubtitlePendingMeasurementGuard worker;
        Assert::IsTrue(worker.RememberAccepted(strong,accepted,Source(before),1000.0/23.976,250));
        SubtitleCueContinuationGuard continuation;
        Assert::IsTrue(continuation.Remember(strong,accepted,base,Source(before)));
        const auto current=Measure(after,3),next=Measure(following,4);
        auto preview=Resolve({current,next});const auto pending=tracker.Consume(preview);
        Assert::IsTrue(pending.detected && pending.confirmationPending);
        preview.current.sharedPicture.required=true;preview.current.sharedPicture.bounds=base;
        preview.current.sharedPicture.identity=preview.current.identity;
        Assert::IsFalse(worker.RememberAccepted(preview,pending,Source(after),1000.0/23.976,250,
            &tracker.AcceptedCueSeedCertificate()));
        Assert::IsFalse(continuation.Remember(preview,pending,base,Source(after)));
        Assert::IsFalse(continuation.AdvanceVerified(preview,pending,Source(after),base,250,1000.0/23.976));
        Assert::IsFalse(continuation.HasReference(),L"provisional output cannot advance the prior missing-edge source cursor");
        auto unresolved=strong;unresolved.available=false;unresolved.current=next;
        unresolved.current.analyzed=false;unresolved.current.text={};unresolved.current.ink.reset();
        Assert::IsFalse(worker.Resolve(unresolved,Source(following),1000.0/23.976,250),
            L"the rejected provisional seed cannot manufacture a late-worker lease");
    }
    TEST_METHOD(PendingCurrentCompositionMayEraseThisFrameButCannotRenewArCaptionLease) {
        for(bool p210:{false,true})for(auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground}) {
            auto before=Pixels(12,p210),after=Pixels(8,p210);
            const auto old=Measure(before,1,p210),now=Measure(after,3,p210);
            SubtitleBoxPresentation tracker;SubtitleCutPastePresentation placement;
            const auto first=Acquire(tracker,old);
            const auto firstPreview=Resolve({Measure(before,2,p210)});
            placement.Consume(first,firstPreview);
            const auto preview=Awaiting(now);const auto pending=tracker.Consume(preview);
            const auto composition=placement.Consume(pending,preview);
            Assert::IsTrue(pending.confirmationPending && composition.valid);
            const ActivePictureBounds base{0,45,Width,315,Width,Height,0.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            auto source=Source(after,p210);const auto raw=EvaluateActivePicturePresentationRetention(source,base);
            QualifiedSubtitlePresentationInput input;input.mode=mode;input.sourcePrepared=true;
            input.currentIdentity=now.identity;input.observation=&now;input.accepted=&pending;
            input.composition=&composition;input.physicalBase=base;input.policyGeneration=7;input.continuityGeneration=8;
            const auto semantic=EvaluateQualifiedSubtitlePresentationEvidence(source,raw,input);
            Assert::IsFalse(semantic.qualified);
            const auto prepared=EvaluatePreparedSubtitlePresentationEvidence(source,raw,input);
            const std::string detail=std::string("prepared handoff reason=")+prepared.reason;
            Logger::WriteMessage(detail.c_str());
            Assert::IsTrue(prepared.qualified && prepared.preparedErasureOnly && !prepared.renewsHold);
            const auto emptyLease=CommitQualifiedSubtitlePresentationHold({},prepared,now.identity,base,mode,7,8,200,true,false,1000000);
            Assert::IsFalse(emptyLease.available);
            auto oldSource=Source(before,p210);
            const auto oldRaw=EvaluateActivePicturePresentationRetention(oldSource,base);
            auto oldInput=input;oldInput.currentIdentity=firstPreview.current.identity;
            oldInput.observation=&firstPreview.current;oldInput.accepted=&first;
            auto firstPlacement=SubtitleCutPastePresentation{}.Consume(first,firstPreview);
            oldInput.composition=&firstPlacement;
            const auto oldProof=EvaluateQualifiedSubtitlePresentationEvidence(oldSource,oldRaw,oldInput);
            const std::string oldDetail=std::string("old strong caption qualification=")+oldProof.reason;
            Logger::WriteMessage(oldDetail.c_str());
            Assert::IsTrue(oldProof.qualified && oldProof.renewsHold);
            const auto acquiredLease=CommitQualifiedSubtitlePresentationHold({},oldProof,firstPreview.current.identity,
                base,mode,7,8,100,true,false,1000000);
            Assert::IsTrue(acquiredLease.available);
            const auto retainedLease=CommitQualifiedSubtitlePresentationHold(acquiredLease,prepared,now.identity,
                base,mode,7,8,200,true,false,1000000);
            Assert::IsTrue(retainedLease.available);
            Assert::AreEqual(acquiredLease.verifiedTick,retainedLease.verifiedTick);
            Assert::IsTrue(SameAnalysisFrameIdentity(acquiredLease.proof.identity,retainedLease.proof.identity));
            const auto failed=CommitQualifiedSubtitlePresentationHold(acquiredLease,prepared,now.identity,
                base,mode,7,8,300,false,false,1000000);
            Assert::AreEqual(acquiredLease.verifiedTick,failed.verifiedTick);
            Assert::IsTrue(SameAnalysisFrameIdentity(acquiredLease.proof.identity,failed.proof.identity));
            Assert::IsTrue(raw.activePicture.classification==prepared.retention.activePicture.classification);
            Assert::IsTrue(raw.activePicture.authorityOrigin==prepared.retention.activePicture.authorityOrigin);
        }
    }
    TEST_METHOD(ReducedDisplayKeepsPlacementButCaptureAndErasureUseOnlyCurrentCaption) {
        for(bool p210:{false,true})for(bool top:{false,true})for(int reduction:{0,50,75}) {
            auto before=Pixels(12,p210,top),after=Pixels(8,p210,top);
            const auto old=Measure(before,1,p210),now=Measure(after,3,p210);
            SubtitleBoxPresentation tracker;SubtitleCutPastePresentation placement;SubtitleLayoutAnchor anchor;
            const auto accepted=Acquire(tracker,old);
            const auto firstPreview=Resolve({Measure(before,2,p210)});
            const SubtitleBoxPadding padding{8,8,8};
            auto first=placement.Consume(accepted,firstPreview,padding);
            first=ApplySubtitleTextReduction(first,reduction,padding);
            first=anchor.Consume(first,accepted,firstPreview,padding,0);
            Assert::IsTrue(first.valid);
            const auto preview=Awaiting(now);const auto pending=tracker.Consume(preview);
            Assert::IsTrue(pending.detected && pending.confirmationPending);
            auto moved=placement.Consume(pending,preview,padding);
            moved=ApplySubtitleTextReduction(moved,reduction,padding);
            moved=anchor.Consume(moved,pending,preview,padding,0);
            Assert::IsTrue(moved.valid);
            Assert::IsTrue(Same(SubtitleGlyphCaptureBounds(now.text,Width,Height),moved.content));
            Assert::IsTrue(Same(SubtitleGlyphCaptureLines(now.text,Width,Height)[0],moved.glyphLines[0]));
            Assert::IsTrue(moved.content.right<first.content.right);
            Assert::IsTrue(moved.pictureCapture.right<=now.text.capturePanel.right);
            Assert::IsTrue(moved.generatedCleanup.Valid() && moved.generatedCleanup.left>=now.text.sourcePanel.left &&
                moved.generatedCleanup.right<=now.text.sourcePanel.right && moved.generatedCleanup.top>=now.text.sourcePanel.top &&
                moved.generatedCleanup.bottom<=now.text.sourcePanel.bottom);
            Assert::IsTrue(moved.pictureCapture.left>=now.text.capturePanel.left && moved.pictureCapture.top>=now.text.capturePanel.top &&
                moved.pictureCapture.right<=now.text.capturePanel.right && moved.pictureCapture.bottom<=now.text.capturePanel.bottom);
            Assert::IsTrue(moved.generatedCleanup.right<first.generatedCleanup.right,
                L"removed suffix picture pixels cannot be erased using the old cue card");
            Assert::IsTrue(Same(first.destination,moved.destination));
            Assert::AreEqual(first.glyphTranslateX,moved.glyphTranslateX);
            Assert::AreEqual(first.glyphTranslateY,moved.glyphTranslateY);
            auto source=Source(after,p210);const auto rawBefore=ExtractActivePictureEvidence(source);
            (void)SubtitleBoxLookahead::ProveCurrentOwnedContraction(old,now);
            const auto rawAfter=ExtractActivePictureEvidence(source);
            Assert::AreEqual(rawBefore.proposedBounds.top,rawAfter.proposedBounds.top);
            Assert::AreEqual(rawBefore.proposedBounds.bottom,rawAfter.proposedBounds.bottom);
            Assert::IsTrue(rawBefore.classification==rawAfter.classification);
        }
    }
};
}
