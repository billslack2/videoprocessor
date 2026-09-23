#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <vprenderer/InwardCaptionEvidence.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <array>
#include <chrono>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
        // Synthetic source pixels, not a captured reconstruction of the replay.
        struct CaptionReturnPixels
        {
            std::vector<uint16_t> pixels;
            CaptionReturnPixels(int top=22,int bottom=158) : pixels(320*180*2,uint16_t(512<<6))
            {
                Rectangle(0,0,320,180,64);
                Rectangle(0,top,320,bottom,300);
            }
            void Rectangle(int left,int top,int right,int bottom,int code)
            {
                for (int y=top;y<bottom;++y)
                    std::fill(pixels.begin()+y*320+left,pixels.begin()+y*320+right,uint16_t(code<<6));
            }
            void Caption() { Rectangle(110,162,210,168,700); }
            AnalysisLumaSource Source(bool native=false) const
            {
                AnalysisLumaSource source;
                source.data=reinterpret_cast<const uint8_t*>(pixels.data());
                source.dataBytes=pixels.size()*sizeof(uint16_t);
                source.width=320; source.height=180;
                source.rowBytes=source.chromaRowBytes=320*sizeof(uint16_t);
                source.format=native ? AnalysisLumaFormat::P210 : AnalysisLumaFormat::P010;
                source.generation=7;
                return source;
            }
        };
        struct CaptionReturnHistory
        {
            ActivePictureTransitionModel model;
            ActivePictureBounds scope,imax;
            CaptionReturnHistory(bool rememberScope=true)
            {
                CaptionReturnPixels scopePixels,imaxPixels(6,174);
                const auto cleanScope=ExtractActivePictureEvidence(scopePixels.Source());
                const auto cleanImax=ExtractActivePictureEvidence(imaxPixels.Source());
                scope=cleanScope.trustedBounds; imax=cleanImax.trustedBounds;
                uint64_t sequence=1;
                if (rememberScope)
                    for (;sequence<=4;++sequence) model.Observe(MakeActivePictureObservation(cleanScope,sequence,24));
                for (int i=0;i<4;++i,++sequence) model.Observe(MakeActivePictureObservation(cleanImax,sequence,24));
            }
        };
        ActivePictureFrameIdentity CaptionIdentity(uint64_t sequence)
        {
            return {7,sequence,sequence,sequence*417083,11,13,17};
        }
        std::array<BufferedPictureExpansionSample,3> CaptionSamples(
            const ActivePictureEvidence& picture,const AnalysisLumaSource& source,
            const ActivePictureBounds& base)
        {
            std::array<BufferedPictureExpansionSample,3> samples;
            const auto retention=EvaluateActivePicturePresentationRetention(source,base);
            for (size_t i=0;i<samples.size();++i)
            {
                samples[i].identity=CaptionIdentity(100+i);
                samples[i].observation=MakeActivePictureObservation(picture,100+i,24);
                samples[i].retention=retention;
                samples[i].nearBlackEvaluated=true;
            }
            return samples;
        }
    }

    TEST_CLASS(InwardCaptionEvidenceTests)
    {
    public:
        TEST_METHOD(RawCaptionStillBlocksInwardProofAndOldBaseMissesItsPixels)
        {
            CaptionReturnHistory history;
            CaptionReturnPixels frame; frame.Caption();
            const auto source=frame.Source();
            const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
            Assert::IsTrue(raw.proposedBounds.top>180-raw.proposedBounds.bottom);
            const auto old=EvaluateActivePicturePresentationRetention(source,history.imax);
            Assert::IsTrue(old.excludedBandsPixelSafe);
            Assert::IsFalse(old.outwardVisibleBoundsAvailable);
            const auto samples=CaptionSamples(raw,source,history.imax);
            const auto proof=BuildBufferedInwardDecision(samples.data(),3,history.model,history.imax,5,4,19,23);
            Assert::IsFalse(proof.transition.publish);
        }

        TEST_METHOD(KnownScopeCaptionProducesProtectedPictureEvidenceForBothAnalysisFormats)
        {
            for (bool native : {false,true})
            {
                CaptionReturnHistory history;
                CaptionReturnPixels frame; frame.Caption();
                const auto source=frame.Source(native);
                const auto raw=ExtractActivePictureEvidence(source);
                const auto corrected=InspectInwardCaptionEvidence(source,raw,history.imax,history.model);
                Logger::WriteMessage(corrected.reason);
                Assert::IsTrue(corrected.valid,L"Known scope return with localized caption should inspect the new bars.");
                Assert::IsTrue(corrected.picture.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::AreEqual(history.scope.top,corrected.picture.trustedBounds.top);
                Assert::AreEqual(history.scope.bottom,corrected.picture.trustedBounds.bottom);
                Assert::IsFalse(corrected.picture.axisEvidence.vertical.FailedBar());
                Assert::IsTrue(corrected.picture.axisEvidence.horizontal==raw.axisEvidence.horizontal,
                    L"Only the independently checked vertical hypothesis may repair axis evidence.");
                Assert::IsTrue(corrected.protectedBounds.top<=history.scope.top);
                Assert::IsTrue(corrected.protectedBounds.bottom>=168);
                Assert::AreEqual(0,corrected.protectedBounds.left);
                Assert::AreEqual(320,corrected.protectedBounds.right);
                Assert::IsTrue(corrected.protectedBounds.top>0 || corrected.protectedBounds.bottom<180,
                    L"The caption has bounded extent; this fixture does not need full raster.");
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL,
                    L"Raw evidence must remain separate from the certified hypothesis.");
            }
        }

        TEST_METHOD(CertifiedReturnUsesExistingInwardProofAndPreservesCaptionOnPublicationFrame)
        {
            CaptionReturnHistory history;
            CaptionReturnPixels frame; frame.Caption();
            const auto source=frame.Source();
            const auto raw=ExtractActivePictureEvidence(source);
            const auto corrected=InspectInwardCaptionEvidence(source,raw,history.imax,history.model);
            Logger::WriteMessage(corrected.reason);
            const auto measured="candidate="+std::to_string(corrected.picture.trustedBounds.top)+".."+std::to_string(corrected.picture.trustedBounds.bottom)+" protected="+std::to_string(corrected.protectedBounds.top)+".."+std::to_string(corrected.protectedBounds.bottom);
            Logger::WriteMessage(measured.c_str());
            Assert::IsTrue(corrected.valid);
            const auto samples=CaptionSamples(corrected.picture,source,history.imax);
            const auto proof=BuildBufferedInwardDecision(samples.data(),3,history.model,history.imax,5,4,19,23);
            Assert::IsTrue(proof.transition.publish);
            Assert::IsTrue(proof.inwardProof==ActivePictureInwardProofValidation::ACCEPTED);
            Assert::IsTrue(proof.proofFrameCount>=2);
            Assert::AreEqual(uint64_t{100},proof.effectiveIdentity.acceptedSequence);
            Assert::IsTrue(history.model.AdoptPublishedDecision(proof.transition,corrected.picture.classification,
                false,nullptr,&corrected.picture.axisEvidence));
            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.geometry=history.imax; crop.rasterWidth=320; crop.rasterHeight=180;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=7; crop.frameSourceSequence=99;
            const auto prior=AdmitCropPresentation({},crop,Evaluate(crop),13).state;
            // This certificate supplies authority and protected placement as one
            // publication transaction; neither raw asymmetry nor safety is lost.
            crop.geometry=proof.transition.bounds; crop.frameSourceSequence=100;
            crop.latestObservationClassification=corrected.picture.classification;
            crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
            crop.outwardExpansion=corrected.protectedBounds; crop.outwardExpansionSourceGeneration=7;
            const auto shown=AdmitCropPresentation(prior,crop,Evaluate(crop),13);
            Assert::IsFalse(shown.blocked);
            Assert::IsTrue(shown.presentation.applyCrop && shown.presentation.outwardExpanded);
            Assert::AreEqual(history.scope.top,shown.state.trustedCrop.top);
            Assert::IsTrue(shown.presentation.sourceBounds.top<=history.scope.top);
            Assert::IsTrue(shown.presentation.sourceBounds.bottom>=168);
            Assert::IsFalse(shown.presentation.verticallyTranslated);
            // Caption disappears: ordinary clean extraction can restore exact
            // scope without waiting for a made-up clearance animation.
            frame.Rectangle(110,162,210,168,64);
            const auto clean=ExtractActivePictureEvidence(frame.Source());
            crop.geometry=clean.trustedBounds; crop.frameSourceSequence=103;
            crop.outwardPresentationActive=crop.outwardExpansionAvailable=false;
            const auto cleared=AdmitCropPresentation(shown.state,crop,Evaluate(crop),13);
            Assert::IsFalse(cleared.blocked);
            Assert::AreEqual(history.scope.top,cleared.presentation.sourceBounds.top);
            Assert::AreEqual(history.scope.bottom,cleared.presentation.sourceBounds.bottom);
        }

        TEST_METHOD(CertifiedCaptionDoesNotBypassLookaheadContinuityOrAvailableFrameBudget)
        {
            CaptionReturnHistory history;
            CaptionReturnPixels frame; frame.Caption();
            const auto source=frame.Source();
            const auto corrected=InspectInwardCaptionEvidence(source,ExtractActivePictureEvidence(source),history.imax,history.model);
            Logger::WriteMessage(corrected.reason);
            const auto measured="candidate="+std::to_string(corrected.picture.trustedBounds.top)+".."+std::to_string(corrected.picture.trustedBounds.bottom)+" protected="+std::to_string(corrected.protectedBounds.top)+".."+std::to_string(corrected.protectedBounds.bottom);
            Logger::WriteMessage(measured.c_str());
            Assert::IsTrue(corrected.valid);
            for (int fault=0;fault<7;++fault)
            {
                auto samples=CaptionSamples(corrected.picture,source,history.imax);
                size_t count=3; uint8_t configured=5,available=4;
                switch (fault)
                {
                case 0: count=1; break;
                case 1: configured=0; break;
                case 2: available=0; break;
                case 3: samples[1].identity.acceptedSequence=100; break;
                case 4: ++samples[1].identity.transportGeneration; break;
                case 5: samples[1].identity.sourceFrameNumber=100; break;
                case 6: samples[1].retention.globalNearBlack=true; break;
                }
                const auto proof=BuildBufferedInwardDecision(samples.data(),count,history.model,history.imax,
                    configured,available,19,23);
                Assert::IsFalse(proof.transition.publish);
            }
        }

        TEST_METHOD(UnknownScopeHistoryCannotBeInventedFromSparseCaptionOrArtwork)
        {
            CaptionReturnHistory history(false);
            CaptionReturnPixels frame; frame.Caption();
            const auto source=frame.Source();
            const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsTrue(EvaluateSymmetricVerticalBarHypothesis(source,raw).classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::IsFalse(InspectInwardCaptionEvidence(source,raw,history.imax,history.model).valid,
                L"The startup symmetry heuristic alone cannot establish a new live format.");
        }

        TEST_METHOD(AsymmetricPictureMenusTwoEdgesAndDarknessCannotAcquireCaptionReturn)
        {
            CaptionReturnHistory history;
            for (int fault=0;fault<6;++fault)
            {
                CaptionReturnPixels frame; frame.Caption();
                auto base=history.imax;
                switch (fault)
                {
                case 0: frame.Rectangle(0,158,320,174,300); break;
                case 1: frame.Rectangle(0,158,320,180,600); break;
                case 2: frame.Rectangle(110,8,210,18,700); break;
                case 3: frame.Rectangle(0,0,320,180,64); frame.Caption(); break;
                case 4: base.left=4; break;
                case 5: frame.Rectangle(0,0,320,22,300); break;
                }
                const auto source=frame.Source();
                const auto result=InspectInwardCaptionEvidence(source,ExtractActivePictureEvidence(source),base,history.model);
                Assert::IsFalse(result.valid);
            }
        }

        TEST_METHOD(CleanScopeAndInvalidSourceDoNotUseCaptionException)
        {
            CaptionReturnHistory history;
            CaptionReturnPixels frame;
            const auto source=frame.Source();
            const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::IsFalse(InspectInwardCaptionEvidence(source,raw,history.imax,history.model).valid);
            frame.Caption();
            const auto caption=ExtractActivePictureEvidence(frame.Source());
            Assert::IsFalse(InspectInwardCaptionEvidence({},caption,history.imax,history.model).valid);
        }

        TEST_METHOD(SourceSizedSyntheticCaptionKeepsProtectionAfterModelAcceptsScope)
        {
            // Source-sized synthetic case using logged vertical coordinates;
            // these uniform pixels are not the recorded movie frame.
            std::vector<uint16_t> pixels(3840*2160*3/2,uint16_t(512<<6));
            auto rectangle=[&](int left,int top,int right,int bottom,int code)
            {
                for (int y=top;y<bottom;++y)
                    std::fill(pixels.begin()+size_t(y)*3840+left,
                        pixels.begin()+size_t(y)*3840+right,uint16_t(code<<6));
            };
            auto picture=[&](int top,int bottom)
            {
                rectangle(0,0,3840,2160,64);
                rectangle(0,top,3840,bottom,300);
            };
            AnalysisLumaSource source;
            source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*sizeof(uint16_t);
            source.width=3840; source.height=2160;
            source.rowBytes=source.chromaRowBytes=3840*sizeof(uint16_t);
            source.format=AnalysisLumaFormat::P010; source.generation=7;
            picture(276,1884);
            const auto scope=ExtractActivePictureEvidence(source);
            Assert::IsTrue(scope.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            ActivePictureTransitionModel model;
            for (uint64_t sequence=1;sequence<=4;++sequence)
                model.Observe(MakeActivePictureObservation(scope,sequence,24));
            picture(68,2092);
            const auto imax=ExtractActivePictureEvidence(source);
            Assert::IsTrue(imax.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            for (uint64_t sequence=5;sequence<=8;++sequence)
                model.Observe(MakeActivePictureObservation(imax,sequence,24));
            picture(276,1884);
            rectangle(1200,1960,2600,2016,700);
            const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
            const auto corrected=InspectInwardCaptionEvidence(source,raw,imax.trustedBounds,model);
            Logger::WriteMessage(corrected.reason);
            const auto measured="candidate="+std::to_string(corrected.picture.trustedBounds.top)+".."+std::to_string(corrected.picture.trustedBounds.bottom)+" protected="+std::to_string(corrected.protectedBounds.top)+".."+std::to_string(corrected.protectedBounds.bottom);
            Logger::WriteMessage(measured.c_str());
            Assert::IsTrue(corrected.valid);
            Assert::AreEqual(scope.trustedBounds.top,corrected.picture.trustedBounds.top);
            Assert::AreEqual(scope.trustedBounds.bottom,corrected.picture.trustedBounds.bottom);
            Assert::IsTrue(corrected.protectedBounds.top<=276);
            Assert::IsTrue(corrected.protectedBounds.bottom>=2016 && corrected.protectedBounds.bottom<2092);
            const auto samples=CaptionSamples(corrected.picture,source,imax.trustedBounds);
            const auto proof=BuildBufferedInwardDecision(samples.data(),3,model,imax.trustedBounds,5,4,19,23);
            Assert::IsTrue(proof.transition.publish);
            Assert::IsTrue(model.AdoptPublishedDecision(proof.transition,corrected.picture.classification,
                false,nullptr,&corrected.picture.axisEvidence));
            // Original-base identity is retained only to revalidate protection,
            // not to publish another scope transition on every caption frame.
            for (int frame=0;frame<12;++frame)
            {
                const auto continued=InspectInwardCaptionEvidence(source,raw,imax.trustedBounds,model);
                Assert::IsTrue(continued.valid);
                Assert::AreEqual(corrected.protectedBounds.top,continued.protectedBounds.top);
                Assert::AreEqual(corrected.protectedBounds.bottom,continued.protectedBounds.bottom);
            }
            // Informational local timing, never a machine-speed pass/fail gate.
            const auto started=std::chrono::steady_clock::now();
            int passed=0;
            for (int run=0;run<100;++run)
                for (int queuedAndLive=0;queuedAndLive<4;++queuedAndLive)
                    passed+=InspectInwardCaptionEvidence(source,raw,imax.trustedBounds,model).valid ? 1 : 0;
            const double elapsed=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-started).count();
            Assert::AreEqual(400,passed);
            const auto timing="Synthetic4K inward-caption helper100 batches x4 calls: total_ms="+
                std::to_string(elapsed)+" mean_batch_ms="+std::to_string(elapsed/100.0)+" mean_call_ms="+std::to_string(elapsed/400.0);
            Logger::WriteMessage(timing.c_str());
            // Removing the caption restores the ordinary clean-scope path;
            // the caption-specific certificate must not linger or be needed.
            picture(276,1884);
            const auto clean=ExtractActivePictureEvidence(source);
            Assert::IsTrue(clean.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::AreEqual(scope.trustedBounds.top,clean.trustedBounds.top);
            Assert::AreEqual(scope.trustedBounds.bottom,clean.trustedBounds.bottom);
            Assert::IsFalse(InspectInwardCaptionEvidence(source,clean,imax.trustedBounds,model).valid);
            // A reset drops remembered format authority even for identical
            // pixels and the same caller-supplied former IMAX bounds.
            rectangle(1200,1960,2600,2016,700);
            const auto captionAgain=ExtractActivePictureEvidence(source);
            Assert::IsTrue(InspectInwardCaptionEvidence(source,captionAgain,imax.trustedBounds,model).valid);
            model.Reset();
            Assert::IsFalse(InspectInwardCaptionEvidence(source,captionAgain,imax.trustedBounds,model).valid);
        }

        TEST_METHOD(SourceSizedMeasurementJitterKeepsCanonicalPictureAndCaptionAtomic)
        {
            // Synthetic 4K +/- one detector step around remembered geometry.
            // The transition model may canonicalize this small measurement;
            // protection must match that published contract exactly.
            for (int delta : {-4,4})
            {
                std::vector<uint16_t> pixels(3840*2160*3/2,uint16_t(512<<6));
                auto rectangle=[&](int left,int top,int right,int bottom,int code)
                {
                    for (int y=top;y<bottom;++y)
                        std::fill(pixels.begin()+size_t(y)*3840+left,
                            pixels.begin()+size_t(y)*3840+right,uint16_t(code<<6));
                };
                auto picture=[&](int top,int bottom)
                {
                    rectangle(0,0,3840,2160,64);
                    rectangle(0,top,3840,bottom,300);
                };
                AnalysisLumaSource source;
                source.data=reinterpret_cast<const uint8_t*>(pixels.data());
                source.dataBytes=pixels.size()*sizeof(uint16_t);
                source.width=3840; source.height=2160;
                source.rowBytes=source.chromaRowBytes=3840*sizeof(uint16_t);
                source.format=AnalysisLumaFormat::P010; source.generation=7;
                picture(276,1884);
                const auto scope=ExtractActivePictureEvidence(source);
                ActivePictureTransitionModel model;
                for (uint64_t sequence=1;sequence<=4;++sequence)
                    model.Observe(MakeActivePictureObservation(scope,sequence,24));
                picture(68,2092);
                const auto imax=ExtractActivePictureEvidence(source);
                for (uint64_t sequence=5;sequence<=8;++sequence)
                    model.Observe(MakeActivePictureObservation(imax,sequence,24));
                picture(276+delta,1884-delta);
                rectangle(1200,1960,2600,2016,700);
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                const auto corrected=InspectInwardCaptionEvidence(source,raw,imax.trustedBounds,model);
                Assert::IsTrue(corrected.valid);
                Assert::AreEqual(scope.trustedBounds.top,corrected.picture.trustedBounds.top);
                Assert::AreEqual(scope.trustedBounds.bottom,corrected.picture.trustedBounds.bottom);
                auto samples=CaptionSamples(corrected.picture,source,imax.trustedBounds);
                // A queue may alternate detector-step measurements while all
                // frames show the same remembered format and carry-over caption.
                picture(276-delta,1884+delta);
                rectangle(1200,1960,2600,2016,700);
                const auto alternateRaw=ExtractActivePictureEvidence(source);
                const auto alternate=InspectInwardCaptionEvidence(source,alternateRaw,imax.trustedBounds,model);
                Assert::IsTrue(alternate.valid);
                Assert::AreEqual(corrected.picture.trustedBounds.top,alternate.picture.trustedBounds.top);
                Assert::AreEqual(corrected.picture.trustedBounds.bottom,alternate.picture.trustedBounds.bottom);
                samples[1].observation=MakeActivePictureObservation(alternate.picture,101,24);
                picture(276+delta,1884-delta);
                rectangle(1200,1960,2600,2016,700);
                const auto proof=BuildBufferedInwardDecision(samples.data(),3,model,imax.trustedBounds,5,4,19,23);
                Assert::IsTrue(proof.transition.publish);
                Assert::AreEqual(corrected.picture.trustedBounds.top,proof.transition.bounds.top);
                Assert::AreEqual(corrected.picture.trustedBounds.bottom,proof.transition.bounds.bottom);
                Assert::IsTrue(model.AdoptPublishedDecision(proof.transition,corrected.picture.classification,
                    false,nullptr,&corrected.picture.axisEvidence));
                Input crop;
                crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
                crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.geometry=imax.trustedBounds; crop.rasterWidth=3840; crop.rasterHeight=2160;
                crop.geometrySourceGeneration=crop.frameSourceGeneration=7; crop.frameSourceSequence=99;
                const auto prior=AdmitCropPresentation({},crop,Evaluate(crop),13).state;
                crop.geometry=proof.transition.bounds; crop.frameSourceSequence=100;
                crop.latestObservationClassification=corrected.picture.classification;
                crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
                crop.outwardExpansion=corrected.protectedBounds; crop.outwardExpansionSourceGeneration=7;
                const auto shown=AdmitCropPresentation(prior,crop,Evaluate(crop),13);
                Assert::IsFalse(shown.blocked);
                Assert::IsTrue(shown.presentation.applyCrop && shown.presentation.outwardExpanded);
                Assert::IsTrue(shown.presentation.sourceBounds.top<=276+delta);
                Assert::IsTrue(shown.presentation.sourceBounds.bottom>=2016);
                Assert::IsFalse(shown.presentation.verticallyTranslated);
                const auto continued=InspectInwardCaptionEvidence(source,raw,imax.trustedBounds,model);
                Assert::IsTrue(continued.valid);
                Assert::AreEqual(proof.transition.bounds.top,continued.picture.trustedBounds.top);
                Assert::AreEqual(proof.transition.bounds.bottom,continued.picture.trustedBounds.bottom);
            }
        }
        TEST_METHOD(NativeV210AndConvertedP010ProduceSameProtectedCaptionCertificate)
        {
            CaptionReturnHistory history;
            CaptionReturnPixels planar; planar.Caption();
            constexpr int pitch=1024;
            std::vector<uint32_t> packed(pitch/4*180,0);
            for (int y=0;y<180;++y)
                for (int x=0;x<320;x+=6)
                {
                    uint32_t luma[6];
                    for (int i=0;i<6;++i)
                        luma[i]=x+i<320 ? planar.pixels[y*320+x+i]>>6 : 64;
                    auto* word=packed.data()+y*(pitch/4)+(x/6)*4;
                    word[0]=512u|(luma[0]<<10)|(512u<<20);
                    word[1]=luma[1]|(512u<<10)|(luma[2]<<20);
                    word[2]=512u|(luma[3]<<10)|(512u<<20);
                    word[3]=luma[4]|(512u<<10)|(luma[5]<<20);
                }
            AnalysisLumaSource native;
            native.data=reinterpret_cast<const uint8_t*>(packed.data());
            native.dataBytes=packed.size()*sizeof(uint32_t);
            native.width=320; native.height=180; native.rowBytes=pitch;
            native.format=AnalysisLumaFormat::NativeYuv422;
            native.encoding=VideoFrameEncoding::V210; native.generation=7;
            Assert::IsTrue(native.IsValid());
            const auto planarSource=planar.Source();
            const auto planarRaw=ExtractActivePictureEvidence(planarSource);
            const auto nativeRaw=ExtractActivePictureEvidence(native);
            Assert::IsTrue(planarRaw.classification==nativeRaw.classification);
            const auto converted=InspectInwardCaptionEvidence(planarSource,planarRaw,history.imax,history.model);
            const auto direct=InspectInwardCaptionEvidence(native,nativeRaw,history.imax,history.model);
            Logger::WriteMessage(converted.reason); Logger::WriteMessage(direct.reason);
            const auto compare="converted="+std::to_string(converted.picture.trustedBounds.top)+".."+std::to_string(converted.picture.trustedBounds.bottom)+" native="+std::to_string(direct.picture.trustedBounds.top)+".."+std::to_string(direct.picture.trustedBounds.bottom);
            Logger::WriteMessage(compare.c_str());
            Assert::IsTrue(converted.valid && direct.valid);
            Assert::AreEqual(converted.picture.trustedBounds.top,direct.picture.trustedBounds.top);
            Assert::AreEqual(converted.picture.trustedBounds.bottom,direct.picture.trustedBounds.bottom);
            Assert::AreEqual(converted.protectedBounds.top,direct.protectedBounds.top);
            Assert::AreEqual(converted.protectedBounds.bottom,direct.protectedBounds.bottom);
            Assert::AreEqual(converted.protectedBounds.left,direct.protectedBounds.left);
            Assert::AreEqual(converted.protectedBounds.right,direct.protectedBounds.right);
        }
    };
}
