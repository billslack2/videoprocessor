#include "pch.h"

#include <ActivePictureEvidence.h>
#include <ActivePictureDecisionTimeline.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include "CppUnitTest.h"

#include <vector>
#include <chrono>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	namespace
	{
		void WriteCode(uint8_t* target, int code)
		{
			const uint16_t packed = static_cast<uint16_t>(code << 6);
			target[0] = static_cast<uint8_t>(packed & 0xff);
			target[1] = static_cast<uint8_t>(packed >> 8);
		}

		struct P010Frame
		{
			int width;
			int height;
			size_t pitch;
			int chromaRows;
			bool usesFullHeightChroma;
			std::vector<uint8_t> bytes;

			P010Frame(int frameWidth, int frameHeight, size_t padding = 0,
				bool fullHeightChroma = false) :
				width(frameWidth),
				height(frameHeight),
				pitch(static_cast<size_t>(frameWidth) * 2 + padding),
				chromaRows(fullHeightChroma ? frameHeight : frameHeight / 2),
				usesFullHeightChroma(fullHeightChroma),
				bytes(pitch * frameHeight + pitch * chromaRows, 0)
			{
				Fill(300, 512, 512);
			}

			void Fill(int y, int u, int v)
			{
				for (int row = 0; row < height; ++row)
					for (int x = 0; x < width; ++x)
						WriteCode(bytes.data() + static_cast<size_t>(row) *
							pitch + x * 2, y);
				const size_t uvOffset = pitch * height;
				for (int row = 0; row < chromaRows; ++row)
					for (int x = 0; x < width; x += 2)
					{
						uint8_t* pixel = bytes.data() + uvOffset +
							static_cast<size_t>(row) * pitch + x * 2;
						WriteCode(pixel, u);
						WriteCode(pixel + 2, v);
					}
			}

			void BlackOutside(int left, int top, int right, int bottom,
				int y = 64, int u = 512, int v = 512)
			{
				for (int row = 0; row < height; ++row)
					for (int x = 0; x < width; ++x)
						if (x < left || x >= right ||
							row < top || row >= bottom)
							WriteCode(bytes.data() +
								static_cast<size_t>(row) * pitch + x * 2, y);
				const size_t uvOffset = pitch * height;
				for (int row = 0; row < chromaRows; ++row)
					for (int x = 0; x < width; x += 2)
						if (x < left || x >= right ||
							(usesFullHeightChroma ? row : row * 2) < top ||
							(usesFullHeightChroma ? row : row * 2) >= bottom)
						{
							uint8_t* pixel = bytes.data() + uvOffset +
								static_cast<size_t>(row) * pitch + x * 2;
							WriteCode(pixel, u);
							WriteCode(pixel + 2, v);
						}
			}

			void FillRectangle(int left, int top, int right, int bottom,
				int y, int u = 512, int v = 512)
			{
				for (int row = top; row < bottom; ++row)
					for (int x = left; x < right; ++x)
						WriteCode(bytes.data() +
							static_cast<size_t>(row) * pitch + x * 2, y);
				const size_t uvOffset = pitch * height;
				const int chromaTop = usesFullHeightChroma ? top : top / 2;
				const int chromaBottom = usesFullHeightChroma ? bottom :
					(bottom + 1) / 2;
				for (int row = chromaTop; row < chromaBottom; ++row)
					for (int x = left & ~1; x < right; x += 2)
					{
						uint8_t* pixel = bytes.data() + uvOffset +
							static_cast<size_t>(row) * pitch + x * 2;
						WriteCode(pixel, u);
						WriteCode(pixel + 2, v);
					}
			}

			P010PlaneView View(size_t lengthAdjustment = 0) const
			{
				const size_t p010Bytes = pitch * height + pitch * (height / 2);
				return { bytes.data(), p010Bytes - lengthAdjustment,
					width, height, pitch, pitch };
			}

			AnalysisLumaSource P210Source() const
			{
				return { bytes.data(), bytes.size(), width, height, pitch, pitch,
					AnalysisLumaFormat::P210, VideoFrameEncoding::V210,
					ColorSpace::REC_709, 1 };
			}

			AnalysisLumaSource P010Source() const
			{
				const size_t p010Bytes = pitch * height + pitch * (height / 2);
				return { bytes.data(), p010Bytes, width, height, pitch, pitch,
					AnalysisLumaFormat::P010, VideoFrameEncoding::UNKNOWN,
					ColorSpace::REC_709, 1 };
			}
		};

		ActivePictureBounds ScopePresentation(int width, int height,
			int top, int bottom)
		{
			return { 0, top, width, bottom, width, height,
				static_cast<double>(width) / (bottom - top),
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}
        struct ContainedInspectionFixture
        {
            AlphaSourceCrop::VerticalInspectionBridgeState inspection;
            AlphaSourceCrop::PresentationRecoveryInput recovery;
        };

        ContainedInspectionFixture MakeContainedInspectionFixture()
        {
            using namespace AlphaSourceCrop;
            ContainedInspectionFixture fixture;
            auto& input=fixture.recovery;
            auto& crop=input.crop;
            P010Frame native(3840,2160);
            native.BlackOutside(0,276,3840,1884);
            const auto nativeEvidence=ExtractActivePictureEvidence(native.P010Source());
            Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),int(nativeEvidence.classification));
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
            crop.geometry=nativeEvidence.trustedBounds; crop.classification=nativeEvidence.classification;
            crop.latestObservationClassification=nativeEvidence.classification;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=1;
            crop.frameSourceSequence=100; crop.rasterWidth=3840; crop.rasterHeight=2160;
            const auto scope=AdmitCropPresentation({},crop,Evaluate(crop),4);
            Assert::IsTrue(scope.state.available && scope.presentation.applyCrop && !scope.blocked);
            auto fitCrop=crop;
            fitCrop.frameSourceSequence=101;
            fitCrop.outwardPresentationActive=fitCrop.outwardExpansionAvailable=true;
            fitCrop.outwardExpansionSourceGeneration=1; fitCrop.outwardExpansion=crop.geometry;
            fitCrop.outwardExpansion.top=34; fitCrop.outwardExpansion.bottom=2126;
            const auto fit=AdmitCropPresentation(scope.state,fitCrop,Evaluate(fitCrop),4);
            Assert::IsTrue(fit.state.outwardPresentationAvailable && fit.presentation.applyCrop);
            Assert::AreEqual(34,fit.presentation.sourceBounds.top);
            Assert::AreEqual(2126,fit.presentation.sourceBounds.bottom);
            input.previousAdmission=fit.state;
            crop.latestObservationSupportsCrop=false; crop.latestObservationIsProvisional=true;
            crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated=true;
            crop.currentVisibleBoundsAvailable=true; crop.currentVisibleBase=crop.geometry;
            crop.currentVisibleSourceGeneration=1; crop.currentVisibleBounds=crop.geometry;
            crop.currentVisibleBounds.top=85; crop.currentVisibleBounds.bottom=2105;
            crop.currentVisibleBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            input.measurementCurrent=input.retentionEvaluated=input.nearBlackEvaluated=true;
            input.retentionBounds=crop.geometry; input.retentionSourceGeneration=1;
            input.observationAvailable=true; input.observationClassification=ActivePictureClassification::PROVISIONAL;
            input.observation=crop.currentVisibleBounds;
            input.observation.top=188; input.observation.bottom=2092;
            input.framesPerSecond=24; input.presentationEpoch=4;
            for (uint64_t sequence=102;sequence<=106;++sequence)
            {
                crop.frameSourceSequence=input.retentionSourceSequence=crop.currentVisibleSourceSequence=sequence;
                VerticalInspectionBridgeInput inspect;
                inspect.previous=fixture.inspection; inspect.candidate=inspect.retentionRequested=true;
                inspect.sourceGeneration=1; inspect.presentationEpoch=4; inspect.sourceSequence=sequence;
                inspect.trustedBase=crop.geometry;
                const auto bridge=UpdateVerticalInspectionBridge(inspect);
                fixture.inspection=bridge.state;
                crop.verticalInspectionPending=bridge.retain;
                crop.verticalInspectionSourceGeneration=1; crop.verticalInspectionSourceSequence=sequence;
                crop.presentationFailOpen=bridge.state.failOpenLatched;
                input.candidate=Evaluate(crop);
                const auto recovering=EvaluatePresentationRecovery(input);
                Assert::IsTrue(recovering.state.active && recovering.presentation.applyCrop);
                Assert::AreEqual(34,recovering.presentation.sourceBounds.top);
                Assert::AreEqual(2126,recovering.presentation.sourceBounds.bottom);
                input.previous=recovering.state;
                input.previousAdmission=AdmitCropPresentation(input.previousAdmission,crop,
                    recovering.presentation,4).state;
            }
            Assert::IsTrue(fixture.inspection.active && fixture.inspection.failOpenLatched);
            Assert::IsTrue(input.previous.active && input.previousAdmission.outwardPresentationAvailable);
            Assert::AreEqual(0u,input.previous.samples);
            // Synthetic source pixels model the current contained observation.
            // The preceding presentation envelopes reproduce the logged policy sequence,
            // not a pixel-exact reconstruction of the source playback controls.
            P010Frame partial(3840,2160);
            partial.BlackOutside(788,320,3840,1884);
            const auto retention=EvaluateActivePicturePresentationRetention(partial.P010Source(),crop.geometry);
            Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(retention.activePicture.classification));
            Assert::AreEqual(788,retention.activePicture.proposedBounds.left);
            Assert::AreEqual(320,retention.activePicture.proposedBounds.top);
            Assert::AreEqual(1884,retention.activePicture.proposedBounds.bottom);
            Assert::IsTrue(retention.proposedBoundsContained && retention.excludedBandsPixelSafe && retention.currentlyPixelSafe);
            Assert::IsFalse(retention.globalNearBlack || retention.outwardVisibleBoundsAvailable ||
                retention.samplingEquivalent || retention.partialSamplingReaffirmed);
            crop.frameSourceSequence=input.retentionSourceSequence=107;
            crop.currentVisibleBoundsAvailable=false; crop.verticalInspectionPending=false;
            crop.frameLocalPresentationRetentionSafe=retention.CanRetainPresentation();
            input.observation=retention.activePicture.proposedBounds;
            input.excludedBandsPixelSafe=retention.excludedBandsPixelSafe;
            input.globalNearBlack=retention.globalNearBlack;
            input.candidate=Evaluate(crop);
            return fixture;
        }
	}

	TEST_CLASS(ActivePictureEvidenceTests)
	{
	public:
        TEST_METHOD(RelativeContrastMeasuredDarkExpansionUsesBothRealBars)
        {
            for(bool p210 : {false,true}) for(int edge : {64,68}) {
                P010Frame frame(3840,2160,32,p210);
                frame.Fill(64,512,512);
                frame.FillRectangle(0,edge,3840,272,158);
                frame.FillRectangle(0,272,3840,1888,300);
                frame.FillRectangle(0,1888,3840,2160-edge,94);
                auto source=p210?frame.P210Source():frame.P010Source();
                // Geometry is natively certified on a bright frame, then inspected
                // against the dark pixels. This tests corroboration, not extraction.
                P010Frame bright(3840,2160,32,p210); bright.BlackOutside(0,edge,3840,2160-edge);
                auto raw=ExtractActivePictureEvidence(p210?bright.P210Source():bright.P010Source());
                Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                const auto base=ScopePresentation(3840,2160,272,1888);
                const auto proof=InspectRelativeBarContrast(source,raw,base);
                Assert::IsTrue(proof.valid,L"Measured Y94 picture over Y64 bars must corroborate the independently qualified target.");
                Assert::IsTrue(proof.evaluated && proof.samples>0 && proof.samples<=16384);
                Assert::AreEqual(raw.trustedBounds.top,proof.target.top);
                Assert::AreEqual(base.top,proof.base.top);
            }
        }

        TEST_METHOD(RelativeContrastRejectsDeskMissingEdgeAndBarContamination)
        {
            const auto base=ScopePresentation(3840,2160,272,1888);
            P010Frame bright(3840,2160); bright.BlackOutside(0,68,3840,2092);
            const auto raw=ExtractActivePictureEvidence(bright.P010Source());
            for(int fault=0;fault<8;++fault) {
                P010Frame frame(3840,2160); frame.Fill(64,512,512);
                frame.FillRectangle(0,68,3840,272,158);
                frame.FillRectangle(0,272,3840,1888,300);
                frame.FillRectangle(0,1888,3840,2092,94);
                if(fault==0) frame.FillRectangle(0,0,3840,68,158); // unbarred desk
                if(fault==1) frame.FillRectangle(0,1888,3840,2160,64); // no opposite picture
                if(fault==2) frame.FillRectangle(0,2092,3840,2160,94); // picture extends outside
                if(fault==3) frame.FillRectangle(960,0,1920,68,160); // OSD in prospective bar
                if(fault==4) frame.FillRectangle(0,0,3840,68,64,540,512); // colored exterior
                if(fault==5) { frame.FillRectangle(0,1888,3840,2092,64); frame.FillRectangle(1400,1950,2440,1980,700); }
                if(fault==6) { frame.FillRectangle(0,1888,3840,2092,64); frame.FillRectangle(0,1888,500,2092,94); }
                if(fault==7) frame.FillRectangle(960,2098,2880,2099,700); // one caption row between original sparse depths
                Assert::IsFalse(InspectRelativeBarContrast(frame.P010Source(),raw,base).valid);
            }
        }

        TEST_METHOD(RelativeContrastRejectsUncertifiedGeometryAndContext)
        {
            P010Frame frame(3840,2160); frame.BlackOutside(0,68,3840,2092);
            const auto original=ExtractActivePictureEvidence(frame.P010Source());
            for(int fault=0;fault<8;++fault) {
                auto raw=original; auto base=ScopePresentation(3840,2160,272,1888); auto source=frame.P010Source();
                if(fault==0) raw.classification=ActivePictureClassification::PROVISIONAL;
                if(fault==1) raw.top.trusted=false;
                if(fault==2) raw.bottom.trusted=false;
                if(fault==3) raw.axisEvidence.horizontal.scanComplete=false;
                if(fault==4) raw.trustedBounds.bottom-=80;
                if(fault==5) base.rasterWidth=1920;
                if(fault==6) raw.trustedBounds.left=16;
                if(fault==7) source.dataBytes=1;
                Assert::IsFalse(InspectRelativeBarContrast(source,raw,base).valid);
            }
        }

        TEST_METHOD(RelativeContrastNativeV210MatchesPlanarAndRejectsUnknownSides)
        {
            P010Frame frame(960,540,32,true); frame.Fill(64,512,512);
            frame.FillRectangle(0,18,960,68,158); frame.FillRectangle(0,68,960,472,300);
            frame.FillRectangle(0,472,960,522,94);
            P010Frame bright(960,540,32,true); bright.BlackOutside(0,18,960,522);
            auto raw=ExtractActivePictureEvidence(bright.P210Source());
            const auto base=ScopePresentation(960,540,68,472);
            const auto planar=frame.P210Source(); const size_t pitch=960/6*16+32;
            std::vector<uint8_t> bytes(pitch*540,0);
            for(int y=0;y<540;++y) for(int x=0;x<960;x+=6) {
                AnalysisLumaSample p[6];
                for(int i=0;i<6;++i) Assert::IsTrue(planar.Sample(x+i,y,p[i]));
                const uint32_t words[]={
                    uint32_t(p[0].chromaU)|uint32_t(p[0].luma)<<10|uint32_t(p[0].chromaV)<<20,
                    uint32_t(p[1].luma)|uint32_t(p[2].chromaU)<<10|uint32_t(p[2].luma)<<20,
                    uint32_t(p[2].chromaV)|uint32_t(p[3].luma)<<10|uint32_t(p[4].chromaU)<<20,
                    uint32_t(p[4].luma)|uint32_t(p[4].chromaV)<<10|uint32_t(p[5].luma)<<20};
                auto* target=bytes.data()+size_t(y)*pitch+size_t(x/6)*16;
                for(int word=0;word<4;++word) for(int b=0;b<4;++b)
                    target[word*4+b]=static_cast<uint8_t>(words[word]>>(b*8));
            }
            AnalysisLumaSource native{bytes.data(),bytes.size(),960,540,pitch,0,
                AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,1};
            auto a=InspectRelativeBarContrast(planar,raw,base), b=InspectRelativeBarContrast(native,raw,base);
            Assert::IsTrue(a.valid && b.valid); Assert::AreEqual(a.samples,b.samples);
            raw.axisEvidence.horizontal.state=ActivePictureAxisState::UNKNOWN;
            raw.axisEvidence.horizontal.barCandidate=true;
            raw.axisEvidence.horizontal.reason=ActivePictureAxisReason::BAR_EDGE_REJECTED;
            Assert::IsTrue(InspectRelativeBarContrast(native,raw,base).valid,
                L"No horizontal pixels are discarded, so rejected side artwork must not veto independently clean vertical bars.");
            raw.axisEvidence.horizontal.reason=ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT;
            Assert::IsFalse(InspectRelativeBarContrast(native,raw,base).valid);
        }

        TEST_METHOD(RelativeContrastDarkNativeExtractionAndBoundedCost)
        {
            for(bool p210 : {false,true}) {
                P010Frame frame(3840,2160,32,p210); frame.Fill(64,512,512);
                frame.FillRectangle(0,68,3840,272,158);
                frame.FillRectangle(0,272,3840,1888,300);
                frame.FillRectangle(0,1888,3840,2092,94);
                const auto source=p210?frame.P210Source():frame.P010Source();
                const auto before=frame.bytes;
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.available && raw.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
                Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"Dark pixels themselves must supply native geometry before relative corroboration.");
                Assert::IsTrue(raw.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM);
                Assert::AreEqual(0,raw.trustedBounds.left); Assert::AreEqual(3840,raw.trustedBounds.right);
                Assert::IsTrue(raw.top.trusted && raw.bottom.trusted);
                const auto base=ScopePresentation(3840,2160,272,1888);
                auto proof=InspectRelativeBarContrast(source,raw,base);
                Assert::IsTrue(proof.valid);
                const auto started=std::chrono::steady_clock::now();
                for(int i=0;i<20;++i) {
                    proof=InspectRelativeBarContrast(source,raw,base);
                    Assert::IsTrue(proof.valid && proof.samples<=16384);
                }
                const auto elapsed=std::chrono::duration<double,std::micro>(
                    std::chrono::steady_clock::now()-started).count()/20;
                std::ostringstream message;
                message<<"Relative contrast 4K "<<(p210?"P210":"P010")<<" samples="<<proof.samples
                    <<" average_us="<<elapsed<<" (informational; no timing threshold)";
                Logger::WriteMessage(message.str().c_str());
                Assert::IsTrue(before==frame.bytes,L"Inspection must not alter source pixels.");
            }
        }

        TEST_METHOD(DeskConnectedSidePictureCannotAcquireAFalseVerticalCrop)
        {
            for (int height : {720, 1080, 2160})
            for (bool p210 : {false, true})
            for (bool above : {false, true})
            for (bool right : {false, true})
            {
                const int width = height * 16 / 9;
                const int bar = (height / 12) & ~3;
                const int objectWidth = width / 100;
                const int left = right ? width - objectWidth : 0;
                P010Frame frame(width, height, 64, p210);
                frame.BlackOutside(0, bar, width, height - bar);
                // A narrow object/reflection reaches from the retained picture
                // into the proposed bar at a side. It occupies less than 5% of
                // the line, so the old aggregate bar test can accept it.
                frame.FillRectangle(left, above ? 0 : height-bar-16,
                    left+objectWidth, above ? bar+16 : height, 200);
                const auto source = p210 ? frame.P210Source() : frame.P010Source();
                const auto raw = ExtractActivePictureEvidence(source);
                const auto startup = EvaluateSymmetricVerticalBarHypothesis(source, raw);
                ActivePictureTransitionModel model;
                bool acquired = false;
                for (uint64_t sequence = 1; sequence <= 48; ++sequence)
                {
                    const auto decision = model.Observe(MakeActivePictureObservation(startup, sequence, 24));
                    acquired = acquired || (decision.publish && decision.stable &&
                        decision.authoritativeClassification == ActivePictureClassification::BAR_CROP_TRUSTED);
                }
                Assert::IsFalse(acquired,
                    L"Connected side picture inside a proposed bar must not become logical crop authority.");
                Assert::IsFalse(raw.classification == ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::IsFalse(startup.classification == ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"The symmetry fallback must not undo the connected-picture veto.");
            }
        }

        TEST_METHOD(DeskVetoPreservesCleanBarsAndDisconnectedSmallOverlay)
        {
            for (bool p210 : {false, true})
            for (bool overlay : {false, true})
            {
                P010Frame frame(3840, 2160, 64, p210);
                frame.BlackOutside(0, 176, 3840, 1984);
                if (overlay) frame.FillRectangle(0, 20, 32, 52, 200);
                const auto source = p210 ? frame.P210Source() : frame.P010Source();
                const auto raw = ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification == ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"Bright picture touching the sides between real bars is normal scope content.");
                Assert::AreEqual(176, raw.trustedBounds.top);
                Assert::AreEqual(1984, raw.trustedBounds.bottom);
            }
        }

        TEST_METHOD(DeskVetoDoesNotTreatAnIsolatedPixelAsConnectedPicture)
        {
            P010Frame frame(3840, 2160);
            frame.BlackOutside(0, 176, 3840, 1984);
            frame.FillRectangle(22, 0, 23, 192, 200);
            const auto raw = ExtractActivePictureEvidence(frame.P010Source());
            Assert::IsTrue(raw.classification == ActivePictureClassification::BAR_CROP_TRUSTED);
        }

        TEST_METHOD(DeskVetoPreservesOneScanStepFringeAndRejectsDimColoredContinuation)
        {
            for (bool p210 : {false,true})
            for (bool above : {false,true})
            for (bool fringe : {false,true})
            {
                constexpr int width=1920,height=1080,top=88,bottom=992;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,top,width,bottom);
                frame.FillRectangle(0,above ? (fringe ? top-2 : 0) : bottom-8,
                    24,above ? top+8 : (fringe ? bottom+2 : height),
                    fringe ? 200 : 96,fringe ? 512 : 640,512);
                const auto raw=ExtractActivePictureEvidence(p210 ? frame.P210Source() : frame.P010Source());
                if (fringe)
                    Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                        L"One scan step of filtering fringe must not veto genuine bars.");
                else
                    Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL &&
                        raw.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_PICTURE_CONTINUATION,
                        L"Credible dim chroma continuation must veto the proposed bar too.");
            }
        }

        TEST_METHOD(DeskVetoAppliesToNativeV210AndRgbWithoutConversion)
        {
            constexpr int width=960,height=540,bar=44;
            for (bool connected : {false,true})
            {
                P010Frame frame(width,height,32,true);
                frame.BlackOutside(0,bar,width,height-bar);
                if (connected) frame.FillRectangle(0,0,12,bar+12,200);
                const auto planar=frame.P210Source();
                const size_t pitch=width/6*16+32;
                std::vector<uint8_t> bytes(pitch*height,0);
                for(int y=0;y<height;++y) for(int x=0;x<width;x+=6) {
                    AnalysisLumaSample p[6];
                    for(int i=0;i<6;++i) Assert::IsTrue(planar.Sample(x+i,y,p[i]));
                    const uint32_t words[]={
                        uint32_t(p[0].chromaU)|uint32_t(p[0].luma)<<10|uint32_t(p[0].chromaV)<<20,
                        uint32_t(p[1].luma)|uint32_t(p[2].chromaU)<<10|uint32_t(p[2].luma)<<20,
                        uint32_t(p[2].chromaV)|uint32_t(p[3].luma)<<10|uint32_t(p[4].chromaU)<<20,
                        uint32_t(p[4].luma)|uint32_t(p[4].chromaV)<<10|uint32_t(p[5].luma)<<20};
                    auto* target=bytes.data()+size_t(y)*pitch+size_t(x/6)*16;
                    for(int word=0;word<4;++word) for(int b=0;b<4;++b)
                        target[word*4+b]=static_cast<uint8_t>(words[word]>>(b*8));
                }
                AnalysisLumaSource native{bytes.data(),bytes.size(),width,height,pitch,0,
                    AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,1};
                const size_t rgbPitch=width*4+32;
                std::vector<uint8_t> rgb(rgbPitch*height,0);
                for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                    const bool picture=(y>=bar && y<height-bar) || (connected && x<12 && y<bar+12);
                    auto* pixel=rgb.data()+size_t(y)*rgbPitch+x*4;
                    pixel[0]=pixel[1]=pixel[2]=picture ? 80 : 0; pixel[3]=255;
                }
                AnalysisLumaSource rgbSource{rgb.data(),rgb.size(),width,height,rgbPitch,0,
                    AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::BGRA_8BIT,ColorSpace::REC_709,1};
                for (const auto& source : {native,rgbSource}) {
                    const auto raw=ExtractActivePictureEvidence(source);
                    Assert::IsTrue(raw.classification==(connected ? ActivePictureClassification::PROVISIONAL :
                        ActivePictureClassification::BAR_CROP_TRUSTED));
                    if (connected) Assert::IsTrue(raw.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_PICTURE_CONTINUATION);
                }
            }
        }

        TEST_METHOD(DeskVetoChecksActualSymmetricBoundaryWithLocalizedCaption)
        {
            for (bool connected : {false,true})
            {
                P010Frame frame(320,180);
                frame.BlackOutside(0,22,320,158);
                frame.FillRectangle(110,162,210,172,700);
                if (connected) frame.FillRectangle(40,150,44,166,200);
                const auto source=frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                Assert::IsTrue(raw.top.trusted && raw.proposedBounds.bottom>160,
                    L"The test must reach recovery of a different, inferred bottom boundary.");
                const auto recovered=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                if (connected) {
                    Assert::IsTrue(recovered.classification==ActivePictureClassification::PROVISIONAL);
                    Assert::IsTrue(recovered.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_PICTURE_CONTINUATION);
                } else {
                    Assert::IsTrue(recovered.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                    Assert::AreEqual(158,recovered.trustedBounds.bottom);
                }
            }
        }

        TEST_METHOD(DeskVetoDoesNotCountDuplicateColumnsAtSmallResolution)
        {
            P010Frame frame(160,90);
            frame.BlackOutside(0,12,160,78);
            frame.FillRectangle(11,0,12,20,200);
            const auto raw=ExtractActivePictureEvidence(frame.P010Source());
            Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                L"Repeated samples of one physical column cannot supply two-column support.");
        }

        TEST_METHOD(SparseNativeBarsCanContradictVisibleExtentSafety)
        {
            // Characterization of existing sampling, not reconstructed ZDF pixels:
            // a narrow real object continues from the frame edge into bright picture.
            // The opposing dark extents differ by four pixels, inside native tolerance.
            for (bool p210 : {false,true})
            for (bool objectAbove : {false,true})
            for (bool betweenCoarseColumns : {false,true})
            {
                constexpr int width=3840, height=2160;
                const int top=objectAbove ? 168 : 172;
                const int bottom=objectAbove ? 1988 : 1992;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,top,width,bottom);
                // The wider object hits two of the48 coarse columns. Its area
                // remains below the existing black-fraction/texture limits. The
                // narrower object lies between them but hits two dense columns.
                const int left=betweenCoarseColumns ? 64 : 500;
                const int right=betweenCoarseColumns ? 96 : 620;
                frame.FillRectangle(left,objectAbove ? 0 : bottom-8,
                    right,objectAbove ? top+8 : height,200);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto measured=ExtractActivePictureEvidence(source);
                Assert::IsTrue(measured.classification==ActivePictureClassification::PROVISIONAL,
                    L"Dense connected picture must veto the misleading aggregate black-bar evidence.");
                Assert::IsTrue(measured.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_PICTURE_CONTINUATION);
                Assert::AreEqual(top,measured.proposedBounds.top);
                Assert::AreEqual(bottom,measured.proposedBounds.bottom);
                const auto& occupiedEdge=objectAbove ? measured.top : measured.bottom;
                Assert::IsTrue(occupiedEdge.blackFraction>=0.95 && occupiedEdge.continuity==1.0);
                Assert::IsTrue(occupiedEdge.texture<=8.0 && occupiedEdge.lumaP90==64.0);
                if (betweenCoarseColumns)
                    Assert::AreEqual(1.0,occupiedEdge.blackFraction);
                else
                    Assert::IsTrue(occupiedEdge.blackFraction<0.96);

                const auto safety=EvaluateActivePicturePresentationRetention(source,measured.proposedBounds);
                Assert::IsTrue(safety.analysisValid && safety.presentationValid);
                Assert::IsFalse(safety.globalNearBlack);
                Assert::IsFalse(safety.excludedBandsPixelSafe,
                    L"Existing dense visible-extent inspection already detects the occupied prospective bar.");
                Assert::IsFalse(safety.excludedVerticalBandsPixelSafe);
                Assert::IsTrue(safety.excludedHorizontalBandsPixelSafe);
                Assert::IsFalse(safety.CanRetainPresentation());
                Assert::IsTrue(safety.outwardVisibleBoundsAvailable);
                Assert::AreEqual(objectAbove ? 0 : top,safety.outwardVisibleBounds.top);
                Assert::AreEqual(objectAbove ? bottom : height,safety.outwardVisibleBounds.bottom);
                Assert::AreEqual(0,safety.outwardVisibleBounds.left);
                Assert::AreEqual(width,safety.outwardVisibleBounds.right);
            }
        }

        TEST_METHOD(HardEdge42PixelBarsDoNotAcquirePictureClippingCrop)
        {
            // Synthetic hard edge: the native 4-row scan currently rounds 42 to44,
            // while dense retention sees the real picture in the two excluded rows.
            for (bool p210 : { false, true })
            {
                constexpr int width=3840, height=2160, top=42, bottom=2118;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,top,width,bottom);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::IsTrue(raw.trustedBounds.top<=top && raw.trustedBounds.bottom>=bottom,
                    L"Trusted crop must contain all hard-edged picture, including rows between coarse scan lines.");
                const auto safety=EvaluateActivePicturePresentationRetention(source,raw.trustedBounds);
                Assert::IsTrue(safety.CanRetainPresentation(),
                    L"An unchanged clean hard-edge frame must retain its freshly acquired crop.");
                Assert::IsFalse(safety.outwardVisibleBoundsAvailable);
            }
        }

        TEST_METHOD(HardEdgeBarsRemainSafeAcrossVerticalScanPhasesAndResolutions)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            {
                const int width=height*16/9;
                const int step=std::max(2,height/540);
                const int alignedBar=(height/40/step)*step;
                for(int phase=0;phase<step;++phase)
                {
                    const int top=alignedBar+phase, bottom=height-top;
                    P010Frame frame(width,height,64,p210);
                    frame.BlackOutside(0,top,width,bottom);
                    const auto source=p210 ? frame.P210Source() : frame.P010Source();
                    const auto raw=ExtractActivePictureEvidence(source);
                    std::wostringstream message;
                    message<<L"height="<<height<<L" phase="<<phase<<L" P210="<<p210;
                    Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                        message.str().c_str());
                    Assert::IsTrue(raw.trustedBounds.top<=top && raw.trustedBounds.bottom>=bottom,
                        message.str().c_str());
                    const auto safety=EvaluateActivePicturePresentationRetention(source,raw.trustedBounds);
                    Assert::IsTrue(safety.CanRetainPresentation(),message.str().c_str());
                    Assert::IsFalse(safety.outwardVisibleBoundsAvailable,message.str().c_str());
                }
            }
        }

        TEST_METHOD(HardEdgeAlignedBarsRemainPreciselyAcquiredAndRetained)
        {
            // Positive control: an exact boundary already on the coarse grid
            // must not become a weaker crop or require a different black level.
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            {
                const int width=height*16/9, step=std::max(2,height/540);
                const int top=(height/40/step)*step, bottom=height-top;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,top,width,bottom);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::AreEqual(top,raw.trustedBounds.top);
                Assert::AreEqual(bottom,raw.trustedBounds.bottom);
                const auto safety=EvaluateActivePicturePresentationRetention(source,raw.trustedBounds);
                Assert::IsTrue(safety.CanRetainPresentation());
                Assert::IsFalse(safety.outwardVisibleBoundsAvailable);
            }
        }

        TEST_METHOD(HardEdgePillarBarsRemainSafeAcrossHorizontalScanPhases)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            {
                const int width=height*16/9, step=std::max(2,width/960);
                const int alignedBar=(width/12/step)*step;
                for(int phase=0;phase<step;++phase)
                {
                    const int left=alignedBar+phase, right=width-left;
                    P010Frame frame(width,height,64,p210);
                    frame.BlackOutside(left,0,right,height);
                    const auto source=p210 ? frame.P210Source() : frame.P010Source();
                    const auto raw=ExtractActivePictureEvidence(source);
                    std::wostringstream message;
                    message<<L"pillar width="<<width<<L" phase="<<phase<<L" P210="<<p210;
                    Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                        message.str().c_str());
                    Assert::IsTrue(raw.trustedBounds.left<=left && raw.trustedBounds.right>=right,
                        message.str().c_str());
                    Assert::AreEqual(0,raw.trustedBounds.top);
                    Assert::AreEqual(height,raw.trustedBounds.bottom);
                    const auto safety=EvaluateActivePicturePresentationRetention(source,raw.trustedBounds);
                    Assert::IsTrue(safety.CanRetainPresentation(),message.str().c_str());
                    Assert::IsFalse(safety.outwardVisibleBoundsAvailable,message.str().c_str());
                }
            }
        }

        TEST_METHOD(OddHardEdgesPreservePictureWithSubsampledChromaAtBothAxes)
        {
            for(int height : {720,1080,2160})
            for(bool p210 : {false,true})
            {
                const int width=height*16/9;
                const int left=(width/12)|1, top=(height/12)|1;
                const int right=width-left, bottom=height-top;
                P010Frame frame(width,height,64,p210);
                frame.Fill(64,512,512);
                // Odd luma edges share chroma with the neighboring black pixel:
                // 2x2 for P010 and 2x1 for P210. The high-luma colored picture
                // must survive; shared chroma on black luma must not invent an OSD.
                frame.FillRectangle(left,top,right,bottom,300,640,400);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::IsTrue(raw.trustedBounds.left<=left && raw.trustedBounds.right>=right &&
                    raw.trustedBounds.top<=top && raw.trustedBounds.bottom>=bottom,
                    L"Odd luma boundaries must not be rounded into actual picture for chroma alignment.");
                const auto safety=EvaluateActivePicturePresentationRetention(source,raw.trustedBounds);
                Assert::IsTrue(safety.CanRetainPresentation(),
                    L"Acquisition and retention must agree despite chroma sharing at odd edges.");
                Assert::IsFalse(safety.outwardVisibleBoundsAvailable);
            }
        }

        TEST_METHOD(HardEdgeRefinementReservePreservesEvidenceAfterLongCoarseSearch)
        {
            // This broad, hard-edged window leaves only two coarse inspections.
            // Refinement must not consume the remaining coarse budget and
            // discard valid evidence that the existing detector could acquire.
            for(bool p210 : {false,true})
            {
                P010Frame frame(3840,2160,64,p210);
                frame.BlackOutside(448,500,3392,1660);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.available);
                Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::IsTrue(raw.trustedBounds.left<=448 && raw.trustedBounds.top<=500 &&
                    raw.trustedBounds.right>=3392 && raw.trustedBounds.bottom>=1660);
                Assert::IsTrue(EvaluateActivePicturePresentationRetention(source,raw.trustedBounds).CanRetainPresentation());
                Assert::IsTrue(raw.lumaSamples<=30000,
                    L"The independent refinement reserve keeps total rough scanning bounded.");
            }
        }

        TEST_METHOD(HardEdgeRefinementDoesNotInventBarsOnFullOrBlackFrames)
        {
            for(int height : {720,1080,2160})
            for(bool p210 : {false,true})
            for(bool black : {false,true})
            {
                const int width=height*16/9;
                P010Frame frame(width,height,64,p210);
                frame.Fill(black ? 64 : 300,512,512);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                if(black)
                {
                    Assert::IsFalse(raw.available,
                        L"An exhausted all-black search cannot manufacture crop authority.");
                    Assert::IsTrue(raw.classification==ActivePictureClassification::UNAVAILABLE);
                }
                else
                {
                    Assert::IsTrue(raw.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
                    Assert::AreEqual(0,raw.trustedBounds.left);
                    Assert::AreEqual(0,raw.trustedBounds.top);
                    Assert::AreEqual(width,raw.trustedBounds.right);
                    Assert::AreEqual(height,raw.trustedBounds.bottom);
                }
                Assert::IsTrue(raw.lumaSamples<=30000,
                    L"Full/black frames must preserve the bounded native scan cost.");
            }
        }

        TEST_METHOD(GenuineSymmetricBarsAgreeWithVisibleExtentSafety)
        {
            for (bool p210 : {false,true})
            {
                constexpr int width=3840, height=2160, top=168, bottom=1992;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,top,width,bottom);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto measured=ExtractActivePictureEvidence(source);
                Assert::IsTrue(measured.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::IsTrue(measured.top.trusted && measured.bottom.trusted);
                Assert::AreEqual(top,measured.trustedBounds.top);
                Assert::AreEqual(bottom,measured.trustedBounds.bottom);
                const auto safety=EvaluateActivePicturePresentationRetention(source,measured.trustedBounds);
                Assert::IsTrue(safety.analysisValid && safety.presentationValid);
                Assert::IsTrue(safety.excludedBandsPixelSafe && safety.currentlyPixelSafe);
                Assert::IsTrue(safety.CanRetainPresentation());
                Assert::IsFalse(safety.outwardVisibleBoundsAvailable);
            }
        }

        TEST_METHOD(CenteredBarsAndLocalizedStartupCaptionRemainSupportedAcrossPaddedFormats)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            {
                const int width=height*16/9;
                const int bar=(height/8)&~3;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,bar,width,height-bar);
                auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto centered=ExtractActivePictureEvidence(source);
                Assert::IsTrue(centered.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::AreEqual(bar,centered.trustedBounds.top);
                Assert::AreEqual(height-bar,centered.trustedBounds.bottom);
                Assert::IsTrue(centered.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM);
                // Localized text has a black gap from picture and substantial
                // clean width on each side. This is the existing startup use case.
                frame.FillRectangle(width*7/20,height-bar+bar/6,
                    width*13/20,height-bar+bar*2/3,700);
                source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                Assert::IsTrue(startup.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::AreEqual(bar,startup.trustedBounds.top);
                Assert::AreEqual(height-bar,startup.trustedBounds.bottom);
            }
        }

        TEST_METHOD(LoneDarkDeskAtBottomCannotCreateOpposingBarsAcrossPaddedFormats)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            {
                const int width=height*16/9;
                const int desk=(height/8)&~3;
                P010Frame frame(width,height,96,p210);
                frame.FillRectangle(0,height-desk,width,height,64);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.available);
                Assert::AreEqual(0,raw.proposedBounds.top);
                Assert::AreEqual(height-desk,raw.proposedBounds.bottom);
                Assert::IsTrue(raw.classification!=ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"One full-width dark object cannot supply a missing opposing bar.");
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                Assert::IsTrue(startup.classification!=ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"Startup symmetry must not mirror the desk over visible upper picture.");
            }
        }

        TEST_METHOD(DisconnectedInternalBlackDeskLineDoesNotBecomeAnOuterBar)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            {
                const int width=height*16/9;
                P010Frame frame(width,height,32,p210);
                frame.FillRectangle(0,(height*2/3)&~1,width,(height*3/4)&~1,64);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
                Assert::AreEqual(0,raw.trustedBounds.top);
                Assert::AreEqual(height,raw.trustedBounds.bottom);
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                Assert::IsTrue(startup.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
            }
        }

        TEST_METHOD(NeutralUnequalTopBottomExtentsCannotGainNativeVerticalAuthority)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            for (bool deeperTop : {false,true})
            {
                const int width=height*16/9;
                const int bar=(height/8)&~3, excess=(height/32)&~3;
                const int top=bar+(deeperTop ? excess : 0);
                const int bottom=height-bar-(deeperTop ? 0 : excess);
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,top,width,bottom);
                const auto raw=ExtractActivePictureEvidence(p210 ? frame.P210Source() : frame.P010Source());
                Assert::IsTrue(raw.top.trusted && raw.bottom.trusted,
                    L"Both neutral dark extents individually pass; rejection must be geometric, not chroma.");
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                Assert::IsTrue(raw.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_ASYMMETRY);
                Assert::IsTrue(raw.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::NONE);
            }
        }

        TEST_METHOD(StartupSymmetryCannotCropBroadConnectedPictureAtOppositeBoundary)
        {
            bool allSafe=true;
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            for (bool deeperTop : {false,true})
            {
                const int width=height*16/9;
                const int bar=(height/8)&~3, excess=(height/32)&~3;
                const int top=bar+(deeperTop ? excess : 0);
                const int bottom=height-bar-(deeperTop ? 0 : excess);
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,top,width,bottom);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                // The smaller dark extent borders full-width bright picture
                // continuously attached to the rest of the image. These pixels
                // are not localized text separated from picture by black.
                const int pictureY=deeperTop ? bottom-excess/2 : top+excess/2;
                for (int x : {width/8,width/2,width*7/8})
                {
                    AnalysisLumaSample sample;
                    Assert::IsTrue(source.Sample(x,pictureY,sample));
                    Assert::AreEqual(300,int(sample.luma));
                }
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                const bool safe=startup.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
                    (startup.trustedBounds.top<=top && startup.trustedBounds.bottom>=bottom);
                if (!safe)
                {
                    const auto detail="UNSAFE startup height="+std::to_string(height)+" p210="+
                        std::to_string(p210)+" deeperTop="+std::to_string(deeperTop)+
                        " raw="+std::to_string(top)+".."+std::to_string(bottom)+
                        " cropped="+std::to_string(startup.trustedBounds.top)+".."+
                        std::to_string(startup.trustedBounds.bottom)+" reason="+startup.reason;
                    Logger::WriteMessage(detail.c_str());
                }
                allSafe=allSafe && safe;
            }
            Assert::IsTrue(allSafe,L"Symmetry may nominate a candidate, but cannot grant authority that excludes broad connected picture.");
        }

        TEST_METHOD(NativeVerticalSymmetryKeepsExistingEightPixelAllowanceAtFourK)
        {
            for (bool p210 : {false,true})
            for (int difference : {8,12})
            {
                P010Frame frame(3840,2160,64,p210);
                frame.BlackOutside(0,276,3840,1884-difference);
                const auto raw=ExtractActivePictureEvidence(p210 ? frame.P210Source() : frame.P010Source());
                Assert::IsTrue(raw.top.trusted && raw.bottom.trusted);
                Assert::AreEqual(difference==8,
                    raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                if (difference==12)
                    Assert::IsTrue(raw.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_ASYMMETRY);
            }
        }

        TEST_METHOD(LocalizedStartupCaptionCanHaveTwoIndividuallyTrustedEdges)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            for (bool captionAbove : {false,true})
            {
                const int width=height*16/9, bar=(height/8)&~3;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(0,bar,width,height-bar);
                const int lowerFirst=height-bar+bar/6, lowerLast=height-bar+bar*2/3;
                frame.FillRectangle(width*7/20,captionAbove ? height-lowerLast : lowerFirst,
                    width*13/20,captionAbove ? height-lowerFirst : lowerLast,700);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                Assert::IsTrue(raw.top.trusted && raw.bottom.trusted,
                    L"A caption can make both individual edge checks pass; that alone cannot disable startup recovery.");
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                Assert::IsTrue(startup.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                Assert::AreEqual(bar,startup.trustedBounds.top);
                Assert::AreEqual(height-bar,startup.trustedBounds.bottom);
            }
        }

        TEST_METHOD(StartupFallbackPreservesGapSeparatedShallowFullWidthOverlay)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            for (bool overlayAbove : {false,true})
            {
                const int width=height*16/9, bar=(height/8)&~3;
                const int step=std::max(2,height/540);
                P010Frame frame(width,height,96,p210);
                frame.BlackOutside(0,bar,width,height-bar);
                const int lowerFirst=height-bar+bar/2, lowerLast=lowerFirst+2*step;
                frame.FillRectangle(0,overlayAbove ? height-lowerLast : lowerFirst,
                    width,overlayAbove ? height-lowerFirst : lowerLast,700);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                Assert::IsTrue(startup.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"A separated shallow control strip is not connected picture growth at the nominated boundary.");
                Assert::AreEqual(bar,startup.trustedBounds.top);
                Assert::AreEqual(height-bar,startup.trustedBounds.bottom);
            }
        }

        TEST_METHOD(StartupCaptionFallbackKeepsOneScanStepPictureFringeAllowance)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            for (bool captionAbove : {false,true})
            {
                const int width=height*16/9, bar=(height/8)&~3;
                const int step=std::max(2,height/540);
                P010Frame frame(width,height,32,p210);
                frame.BlackOutside(0,bar,width,height-bar);
                const int first=captionAbove ? bar-step : height-bar;
                frame.FillRectangle(0,first,width,first+step,300);
                const int lowerFirst=height-bar+bar/3, lowerLast=height-bar+bar*2/3;
                frame.FillRectangle(width*7/20,captionAbove ? height-lowerLast : lowerFirst,
                    width*13/20,captionAbove ? height-lowerFirst : lowerLast,700);
                const auto source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                Assert::IsTrue(startup.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"The connected-picture veto must not reject one ordinary detector scan step at the edge.");
                Assert::AreEqual(bar,startup.trustedBounds.top);
                Assert::AreEqual(height-bar,startup.trustedBounds.bottom);
            }
        }

        TEST_METHOD(StartupModelDoesNotAcquireMirroredBroadPictureButKeepsBarsAndCaptionAcquisition)
        {
            for (int height : {720,1080,2160})
            for (bool p210 : {false,true})
            for (int variant=0; variant<3; ++variant)
            {
                const int width=height*16/9, bar=(height/8)&~3, excess=(height/32)&~3;
                P010Frame frame(width,height,64,p210);
                auto source=p210 ? frame.P210Source() : frame.P010Source();
                ActivePictureTransitionModel model;
                const auto initial=ExtractActivePictureEvidence(source);
                Assert::IsTrue(initial.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
                const auto full=model.Observe(MakeActivePictureObservation(initial,1,24.0));
                Assert::IsTrue(full.publish && full.stable);
                // variant0=real centered bars,1=localized caption,2=asymmetric
                // dark object plus broad bright picture in the mirrored band.
                frame.BlackOutside(0,bar,width,height-bar-(variant==2 ? excess : 0));
                if (variant==1)
                    frame.FillRectangle(width*7/20,height-bar+bar/6,
                        width*13/20,height-bar+bar*2/3,700);
                source=p210 ? frame.P210Source() : frame.P010Source();
                const auto raw=ExtractActivePictureEvidence(source);
                const auto startup=EvaluateSymmetricVerticalBarHypothesis(source,raw);
                bool acquiredBars=false;
                ActivePictureBounds acquired;
                for (uint64_t sequence=2; sequence<=12; ++sequence)
                {
                    AlphaSourceCrop::TransitionAdmissionInput input;
                    input.evidence=startup;
                    input.sourceGeneration=1; input.sourceSequence=sequence;
                    input.framesPerSecond=24.0;
                    // No previously compatible crop: same bootstrap eligibility
                    // as the renderer after a full-raster/menu presentation.
                    const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(input);
                    const auto decision=model.Observe(admission.observation);
                    if (decision.publish && decision.bounds.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM)
                    {
                        acquiredBars=true;
                        acquired=decision.bounds;
                    }
                }
                if (variant==2 && acquiredBars)
                {
                    const auto detail="MODEL acquired unsafe startup height="+std::to_string(height)+
                        " p210="+std::to_string(p210)+" bounds="+std::to_string(acquired.top)+
                        ".."+std::to_string(acquired.bottom);
                    Logger::WriteMessage(detail.c_str());
                }
                Assert::AreEqual(variant!=2,acquiredBars,
                    L"The helper feeds logical model authority; broad connected picture must not become a startup crop.");
            }
        }

        TEST_METHOD(NativeOwnBoundaryProofPreservesEightRealPictureRowsOutsideSavedCrop)
        {
            // Synthetic source-sized pixels matching the logged rectangles;
            // this is not a reconstruction of the captured Fox logo.
            P010Frame frame(3840,2160);
            frame.BlackOutside(0,280,3840,1880);
            const auto source=frame.P010Source();
            const auto oldCrop=ScopePresentation(3840,2160,280,1872);
            const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::AreEqual(280,raw.trustedBounds.top);
            Assert::AreEqual(1880,raw.trustedBounds.bottom);
            const auto oldProof=EvaluateActivePicturePresentationRetention(source,oldCrop);
            Assert::IsFalse(oldProof.excludedBandsPixelSafe);
            Assert::IsFalse(oldProof.currentlyPixelSafe);
            Assert::IsTrue(oldProof.outwardVisibleBoundsAvailable);
            Assert::IsTrue(oldProof.outwardVisibleBounds.bottom>=1880);
            const auto candidateProof=EvaluateActivePicturePresentationRetention(source,raw.trustedBounds);
            Assert::IsTrue(candidateProof.analysisValid && candidateProof.presentationValid);
            Assert::IsTrue(candidateProof.excludedBandsPixelSafe && candidateProof.currentlyPixelSafe);
            Assert::IsFalse(candidateProof.outwardVisibleBoundsAvailable);
            Assert::IsFalse(candidateProof.globalNearBlack);
            for(int y=1872;y<1880;++y)
            {
                AnalysisLumaSample sample;
                Assert::IsTrue(source.Sample(1920,y,sample));
                Assert::AreEqual(300,static_cast<int>(sample.luma));
            }
            // An independently measured safe candidate does not make the old
            // crop safe: it must replace, never reaffirm, those clipping bounds.
            Assert::IsFalse(AlphaSourceCrop::IsPixelSafeCropReaffirmation(oldCrop,
                raw.trustedBounds,oldProof.excludedBandsPixelSafe));
        }
		TEST_METHOD(SideZoneTelemetryStaysIndependentOfVerifiedVerticalPermission)
		{
			for (bool darkCorner : {true, false})
			{
				P010Frame frame(960,540);
				frame.Fill(160,512,512);
				frame.BlackOutside(0,68,960,472);
				frame.FillRectangle(880,68,960,472,80,640,512);
				frame.FillRectangle(0,68,30,darkCorner ? 169 : 472,darkCorner ? 64 : 100);
				const auto evidence=ExtractActivePictureEvidence(frame.P010Source());
				Assert::AreEqual(0,evidence.axisEvidence.leftPictureMinimum);
				Assert::IsFalse(evidence.axisEvidence.HasBlockingFailedBar(evidence.trustedBounds));
                Assert::IsTrue(evidence.axisEvidence.HasVerifiedVerticalCropProfile(evidence.trustedBounds));
                Assert::AreEqual(0,evidence.trustedBounds.left);Assert::AreEqual(960,evidence.trustedBounds.right);
                Assert::AreEqual(66,evidence.trustedBounds.top);Assert::AreEqual(474,evidence.trustedBounds.bottom);
				Assert::IsTrue(evidence.leftSideProbe.evaluated);
				Assert::IsFalse(evidence.rightSideProbe.evaluated);
				for (int depth=0;depth<3;++depth)
					for (int zone=0;zone<4;++zone)
					{
						const auto& cell=evidence.leftSideProbe.cells[depth*4+zone];
						const int luma=darkCorner ? (zone==0 ? 64 : 160) : 100;
						Assert::AreEqual(luma,cell.meanLuma);
						Assert::AreEqual(luma,cell.peakLuma);
						Assert::AreEqual(luma>112 ? 12 : 0,cell.strong);
						Assert::AreEqual(luma>88 ? 12 : 0,cell.nonBlack);
					}
			}
		}
		TEST_METHOD(DistributedSidePictureAdmitsOnlyVerifiedFullWidthVerticalCrop)
		{
			for (bool leftPicture : {true, false})
			{
				P010Frame initial(960,540), scope(960,540);
				initial.BlackOutside(0,18,960,522);
				scope.Fill(160,512,512);
				scope.BlackOutside(0,68,960,472);
				scope.FillRectangle(leftPicture ? 880 : 0,68,leftPicture ? 960 : 80,472,80,640,512);
				const auto base=ExtractActivePictureEvidence(initial.P010Source());
				const auto evidence=ExtractActivePictureEvidence(scope.P010Source());
				Assert::IsTrue(evidence.axisEvidence.HasFailedBar());
				Assert::IsTrue(evidence.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(evidence.trustedBounds));
				Assert::AreEqual(12,leftPicture ? evidence.axisEvidence.leftPictureMinimum : evidence.axisEvidence.rightPictureMinimum);
				ActivePictureTransitionModel model;
				for (uint64_t seq=1;seq<=4;++seq) model.Observe(MakeActivePictureObservation(base,seq,24));
				bool published=false;
				for (uint64_t seq=5;seq<=12;++seq)
				{
					AlphaSourceCrop::TransitionAdmissionInput input;
					input.evidence=evidence;
					input.retention=EvaluateActivePicturePresentationRetention(scope.P010Source(),base.trustedBounds);
					input.trustedGeometry=input.presentationBeforeObservation=base.trustedBounds;
					input.trustedGeometryAvailable=input.compatiblePresentation=true;
					input.trustedGeneration=input.sourceGeneration=1;
					input.sourceSequence=seq; input.framesPerSecond=24;
					input.outwardCandidate=evidence.trustedBounds;
					const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(input);
					Assert::IsFalse(admission.observation.transitionDeferred);
					const auto decision=model.Observe(admission.observation);
					if (decision.publish)
					{
						published=true;
						Assert::AreEqual(0,decision.bounds.left);
						Assert::AreEqual(960,decision.bounds.right);
						Assert::AreEqual(68,decision.bounds.top);
						Assert::AreEqual(472,decision.bounds.bottom);
					}
				}
				Assert::IsTrue(published);
				// A certificate cannot be borrowed for another aperture or axis.
				auto different=evidence.trustedBounds; different.top+=4;
				Assert::IsTrue(evidence.axisEvidence.HasBlockingFailedBar(different));
				different=evidence.trustedBounds; different.left=4;
				Assert::IsTrue(evidence.axisEvidence.HasBlockingFailedBar(different));
				different=evidence.trustedBounds; different.rasterWidth=1920;
				Assert::IsTrue(evidence.axisEvidence.HasBlockingFailedBar(different));
			}
		}

		TEST_METHOD(VerifiedVerticalCropPreservesSideLogosBordersAndDimMargins)
		{
			for (int pattern=0;pattern<5;++pattern)
			{
				P010Frame frame(960,540);
				frame.BlackOutside(40,68,880,472);
				frame.FillRectangle(880,68,960,472,80,640,512);
				if (pattern==0) frame.FillRectangle(0,180,40,240,700); // corner/side logo
				if (pattern==1) frame.FillRectangle(0,68,2,472,700); // thin border
				if (pattern==2) frame.FillRectangle(0,68,40,472,100); // lifted dark edge
				if (pattern==3) // sparse credits spread over all four zones
					for (int y=80;y<472;y+=100) frame.FillRectangle(0,y,40,y+16,700);
				const auto evidence=ExtractActivePictureEvidence(frame.P010Source());
				Assert::IsTrue(evidence.axisEvidence.HasFailedBar());
				Assert::IsTrue(evidence.axisEvidence.HasVerifiedVerticalCropProfile(evidence.trustedBounds));
                // All authored side graphics stay inside the full-width crop;
                // the certificate removes only the separately verified bars.
                Assert::AreEqual(0,evidence.trustedBounds.left);Assert::AreEqual(960,evidence.trustedBounds.right);
                Assert::AreEqual(66,evidence.trustedBounds.top);Assert::AreEqual(474,evidence.trustedBounds.bottom);
                const auto darkness=EvaluateActivePictureGlobalNearBlack(frame.P010Source());
                Assert::IsTrue(darkness.evaluated && !darkness.nearBlack);
                const auto constrained=ConstrainNearBlackCropAcquisition(evidence,darkness.nearBlack);
                Assert::IsTrue(constrained.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
				Assert::IsFalse(evidence.axisEvidence.HasBlockingFailedBar(evidence.trustedBounds));
			}
		}

		TEST_METHOD(SidePictureInwardQueueRequiresFreshProofInEveryFrame)
		{
			P010Frame initial(960,540), scope(960,540);
			initial.BlackOutside(0,18,960,522);
			scope.BlackOutside(0,68,960,472);
			scope.FillRectangle(880,68,960,472,80,640,512);
			const auto base=ExtractActivePictureEvidence(initial.P010Source());
			const auto evidence=ExtractActivePictureEvidence(scope.P010Source());
			ActivePictureTransitionModel model;
			for (uint64_t seq=1;seq<=4;++seq) model.Observe(MakeActivePictureObservation(base,seq,24));
			AlphaSourceCrop::BufferedPictureExpansionSample samples[5];
			for (int i=0;i<5;++i)
			{
				const uint64_t seq=5+i;
				samples[i].identity={1,seq,seq,seq*1000,1,1,1};
				samples[i].observation=MakeActivePictureObservation(evidence,seq,24);
				samples[i].retention=EvaluateActivePicturePresentationRetention(scope.P010Source(),base.trustedBounds);
				samples[i].nearBlackEvaluated=true;
			}
			const auto build=[&]() { return AlphaSourceCrop::BuildBufferedInwardDecision(samples,5,model,base.trustedBounds,5,4,1,1); };
			const auto proved=build();
			Assert::IsTrue(proved.transition.publish);
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(1);
			ActivePictureFrameDecision queued;
			for (uint64_t seq=1;seq<=4;++seq)
			{
				const ActivePictureFrameIdentity id{1,seq,seq,seq*1000,1,1,1};
				const auto observation=MakeActivePictureObservation(base,seq,24);
				timeline.TrackAcceptedFrame(id);
				timeline.TrackLookaheadEvidence(id,observation,true,false);
				timeline.SubmitScheduledObservation(id,observation,5,4,queued);
			}
			bool associated=false;
			for (const auto& sample : samples)
			{
				timeline.TrackAcceptedFrame(sample.identity);
				timeline.TrackLookaheadEvidence(sample.identity,sample.observation,true,false);
				if (timeline.SubmitScheduledObservation(sample.identity,sample.observation,5,4,queued))
					associated=associated || queued.association==ActivePictureDecisionAssociation::EXACT_INWARD;
			}
			Assert::IsTrue(associated);
			samples[1].observation.axisEvidence.leftPictureMinimum=0;
			Assert::IsFalse(build().transition.publish);
			samples[1].observation.axisEvidence=evidence.axisEvidence;
			samples[1].retention.globalNearBlack=true;
			Assert::IsFalse(build().transition.publish);
		}

		TEST_METHOD(SidePictureProbeUsesNativeP210Luma)
		{
			P010Frame frame(960,540,32,true);
			frame.BlackOutside(0,68,960,472);
			frame.FillRectangle(880,68,960,472,80,640,512);
			const auto evidence=ExtractActivePictureEvidence(frame.P210Source());
			Assert::IsTrue(evidence.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(evidence.trustedBounds));
		}


		TEST_METHOD(VerifiedVerticalPermissionDoesNotAuthorizeUnresolvedHorizontalRemoval)
		{
			for (bool vertical : { true, false })
			{
				P010Frame initial(960,540), inset(960,540);
				if (vertical)
				{
					initial.BlackOutside(0,58,960,482);
					inset.BlackOutside(48,88,912,452);
					inset.FillRectangle(0,88,48,452,64,640,512);
					inset.FillRectangle(912,88,960,452,64,640,512);
				}
				else
				{
					initial.BlackOutside(44,0,916,540);
					inset.BlackOutside(104,48,856,492);
					inset.FillRectangle(104,0,856,48,64,640,512);
					inset.FillRectangle(104,492,856,540,64,640,512);
				}
				const auto base=ExtractP010ActivePictureEvidence(initial.View()).trustedBounds;
				const auto observed=ExtractP010ActivePictureEvidence(inset.View());
				Assert::IsTrue(observed.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
				Assert::IsTrue(observed.trustedBounds.trustedBarAxes==(vertical ?
					ActivePictureBounds::BarAxes::TOP_BOTTOM : ActivePictureBounds::BarAxes::LEFT_RIGHT));
				ActivePictureTransitionModel model;
				for (uint64_t seq=1;seq<=40;++seq)
				{
					const auto source=seq<=4 ? initial.P010Source() : inset.P010Source();
					AlphaSourceCrop::TransitionAdmissionInput input;
					input.evidence=ExtractActivePictureEvidence(source);
					input.retention=EvaluateActivePicturePresentationRetention(source,base);
					input.trustedGeometry=input.presentationBeforeObservation=base;
					input.trustedGeometryAvailable=input.compatiblePresentation=true;
					input.trustedGeneration=input.sourceGeneration=1;
					input.sourceSequence=seq; input.framesPerSecond=24;
					input.outwardCandidate=input.evidence.trustedBounds;
					const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(input);
					if (seq>4)
					{
						Assert::IsFalse(admission.deferPartialComposition);
						Assert::IsFalse(admission.deferOutward);
					}
					const auto decision=model.Observe(admission.observation);
					if (seq>4)
					{
						Assert::AreEqual(vertical && seq==6,decision.publish);
                        // Only verified vertical bands gain authority.
                        // Failed vertical evidence still vetoes a horizontal crop.
                        const auto& actual=decision.publish ? decision.bounds : decision.stableBounds;
                        const auto& expected=vertical && seq>=6 ? observed.trustedBounds : base;
                        if(seq==5)
                        {
                            Assert::IsFalse(decision.publish);
                            Assert::AreEqual(base.top,decision.stableBounds.top);
                            Assert::AreEqual(base.left,decision.stableBounds.left);
                        }
                        Assert::AreEqual(expected.top,actual.top);
                        Assert::AreEqual(expected.bottom,actual.bottom);
                        Assert::AreEqual(expected.left,actual.left);
                        Assert::AreEqual(expected.right,actual.right);
                        if(vertical)
                        {
                            Assert::IsTrue(observed.axisEvidence.HasVerifiedVerticalCropProfile(observed.trustedBounds));
                            Assert::AreEqual(0,decision.bounds.left);Assert::AreEqual(960,decision.bounds.right);
                        }
                        else Assert::IsFalse(observed.axisEvidence.HasVerifiedVerticalCropProfile(observed.trustedBounds));
					}
				}
			}
		}

		TEST_METHOD(AxisMetadataDistinguishesBarsFullExtentUnknownAndUnfinishedScan)
		{
			P010Frame full(960,540), scope(960,540), asymmetric(960,540), raised(960,540), stars(960,540), dark(960,540);
			scope.BlackOutside(0,58,960,482);
			asymmetric.BlackOutside(48,88,904,452);
			raised.BlackOutside(48,88,912,452);
			raised.FillRectangle(0,88,48,452,96);
			raised.FillRectangle(912,88,960,452,96);
			stars.Fill(64,512,512);
			for (int i=0;i<5;++i)
			{
				stars.FillRectangle(i*20+10,0,i*20+12,2,700);
				stars.FillRectangle(i*20+10,538,i*20+12,540,700);
				stars.FillRectangle(0,(i*2+1)*540/96,2,(i*2+1)*540/96+2,700);
				stars.FillRectangle(958,(i*2+1)*540/96,960,(i*2+1)*540/96+2,700);
			}
			dark.Fill(64,512,512);
			const auto f=ExtractP010ActivePictureEvidence(full.View());
			Assert::IsTrue(f.axisEvidence.horizontal.state==ActivePictureAxisState::FULL_EXTENT_SUPPORTED);
			Assert::IsTrue(f.axisEvidence.vertical.state==ActivePictureAxisState::FULL_EXTENT_SUPPORTED);
			const auto c=ExtractP010ActivePictureEvidence(scope.View());
			Assert::IsTrue(c.axisEvidence.vertical.state==ActivePictureAxisState::TRUSTED_BARS);
			Assert::IsTrue(c.axisEvidence.horizontal.state==ActivePictureAxisState::FULL_EXTENT_SUPPORTED);
			const auto a=ExtractP010ActivePictureEvidence(asymmetric.View());
			Assert::IsTrue(a.axisEvidence.horizontal.FailedBar());
			Assert::IsTrue(a.axisEvidence.horizontal.reason==ActivePictureAxisReason::BAR_ASYMMETRY);
			const auto r=ExtractP010ActivePictureEvidence(raised.View());
			Assert::IsTrue(r.axisEvidence.horizontal.state==ActivePictureAxisState::UNKNOWN);
			Assert::IsFalse(r.axisEvidence.horizontal.barCandidate);
			const auto st=ExtractP010ActivePictureEvidence(stars.View());
			Assert::IsTrue(st.axisEvidence.horizontal.state==ActivePictureAxisState::UNKNOWN);
			Assert::IsTrue(st.axisEvidence.vertical.state==ActivePictureAxisState::UNKNOWN);
			const auto d=ExtractP010ActivePictureEvidence(dark.View());
			Assert::IsFalse(d.axisEvidence.horizontal.scanComplete);
			Assert::IsTrue(d.axisEvidence.horizontal.reason==ActivePictureAxisReason::SCAN_INCOMPLETE);
			Assert::IsTrue(d.lumaSamples<30000);
			const auto invalid=ExtractActivePictureEvidence({});
			Assert::IsTrue(invalid.axisEvidence.horizontal.reason==ActivePictureAxisReason::NOT_EVALUATED);
		}

		TEST_METHOD(FailedBarEvidenceCannotReenterThroughHistoryQueueOrPreview)
		{
			P010Frame scope(960,540), inset(960,540), cleanNarrow(960,540), full(960,540);
			scope.BlackOutside(0,58,960,482);
			inset.BlackOutside(48,88,912,452);
			inset.FillRectangle(0,88,48,452,64,640,512);
			inset.FillRectangle(912,88,960,452,64,640,512);
			cleanNarrow.BlackOutside(0,88,960,452);
			const auto base=ExtractP010ActivePictureEvidence(scope.View());
			const auto partial=ExtractP010ActivePictureEvidence(inset.View());
			const auto narrow=ExtractP010ActivePictureEvidence(cleanNarrow.View());
			Assert::IsTrue(partial.axisEvidence.horizontal.FailedBar());
			Assert::IsFalse(narrow.axisEvidence.HasFailedBar());
			ActivePictureTransitionModel live, history, startup;
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(1);
			ActivePictureFrameDecision queued;
			// Put the narrow format in real history, then expand to the base.
			for (uint64_t seq=1;seq<=4;++seq)
				history.Observe(MakeActivePictureObservation(narrow,seq,24));
			for (uint64_t seq=5;seq<=8;++seq)
				history.Observe(MakeActivePictureObservation(base,seq,24));
			for (uint64_t seq=1;seq<=4;++seq)
			{
				const auto obs=MakeActivePictureObservation(base,seq,24);
				live.Observe(obs);
				const ActivePictureFrameIdentity id{1,seq,seq,seq*1000};
				timeline.TrackAcceptedFrame(id);
				timeline.TrackLookaheadEvidence(id,obs,true,false);
				timeline.SubmitScheduledObservation(id,obs,5,4,queued);
			}
			ActivePictureTransitionDecision staleQueue;
			staleQueue.publish=staleQueue.stable=true;
			staleQueue.bounds=narrow.trustedBounds;
			staleQueue.stableBounds=base.trustedBounds;
			ActivePicturePublicationAdmission why;
			Assert::IsFalse(live.AdoptPublishedDecision(staleQueue,narrow.classification,false,&why,&partial.axisEvidence));
			Assert::IsTrue(why==ActivePicturePublicationAdmission::INCOMPLETE_AXIS_RETAINED);
			for (uint64_t seq=9;seq<=40;++seq)
			{
				auto obs=MakeActivePictureObservation(partial,seq,24);
				// The current certificate is bound to its outward guard, not the old narrow crop.
                // Alternating provisional frames must not complete consecutive authority proof.
				if (seq%2==0) obs.classification=ActivePictureClassification::PROVISIONAL;
				Assert::IsFalse(history.Observe(obs).publish);
				const ActivePictureFrameIdentity id{1,seq,seq,seq*1000};
				timeline.TrackAcceptedFrame(id);
				timeline.TrackLookaheadEvidence(id,obs,true,false);
				Assert::IsFalse(timeline.SubmitScheduledObservation(id,obs,5,4,queued));
			}
			// Complete evidence can still accept the identical rectangle.
			Assert::IsTrue(live.AdoptPublishedDecision(staleQueue,narrow.classification,false,&why,&narrow.axisEvidence));
			// Safe full-frame startup and first bar acquisition remain unchanged.
			startup.Observe(MakeActivePictureObservation(ExtractP010ActivePictureEvidence(full.View()),1,24));
			bool acquired=false;
			for (uint64_t seq=2;seq<=8;++seq)
				acquired=startup.Observe(MakeActivePictureObservation(partial,seq,24)).publish || acquired;
			Assert::IsTrue(acquired);
			// An outward move is not denied because an unrelated axis failed.
			auto expansion=MakeActivePictureObservation(base,50,24);
			expansion.axisEvidence=partial.axisEvidence;
			live.Observe(expansion); ++expansion.frameNumber;
			Assert::IsTrue(live.Observe(expansion).publish);
		}

		TEST_METHOD(PixelMultiAspectRoundTripsRemainEligibleWithAxisMetadata)
		{
			for (auto bars : { std::pair<int,int>{208,264}, {264,70}, {208,0} })
			{
				P010Frame first(3840,2160), second(3840,2160);
				first.BlackOutside(0,bars.first,3840,2160-bars.first);
				if (bars.second==0) second.BlackOutside(376,0,3464,2160);
				else second.BlackOutside(0,bars.second,3840,2160-bars.second);
				const auto a=ExtractP010ActivePictureEvidence(first.View());
				const auto b=ExtractP010ActivePictureEvidence(second.View());
				Assert::IsFalse(a.axisEvidence.HasFailedBar());
				Assert::IsFalse(b.axisEvidence.HasFailedBar());
				ActivePictureTransitionModel model;
				uint64_t seq=1;
				for (const auto* evidence : { &a,&b,&a,&b,&a })
				{
					bool accepted=false;
					for (int i=0;i<5;++i)
						accepted=model.Observe(MakeActivePictureObservation(*evidence,seq++,24)).publish || accepted;
					Assert::IsTrue(accepted);
				}
			}
		}

		TEST_METHOD(PixelMeasuredMarginsReplaceOnlyUncertifiedPresentationAxes)
		{
			P010Frame scope(960,540), partial(960,540), subtitle(960,540), expanded(960,540);
			scope.BlackOutside(44,88,916,452);
			partial.BlackOutside(52,92,916,448);
			subtitle.BlackOutside(44,88,916,452);
			subtitle.FillRectangle(200,454,500,460,700);
			const auto base=ExtractP010ActivePictureEvidence(scope.View()).trustedBounds;
			const auto sideEvidence=ExtractP010ActivePictureEvidence(partial.View());
			Assert::IsTrue(sideEvidence.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM);
			const auto safe=EvaluateP010ActivePicturePresentationRetention(partial.View(),base);
			Assert::IsTrue(safe.excludedBandsPixelSafe);
			const auto side=AlphaSourceCrop::ResolvePresentationObservation(base,sideEvidence,safe);
			Assert::AreEqual(base.left,side.bounds.left); Assert::AreEqual(base.right,side.bounds.right);
			const auto textEvidence=ExtractP010ActivePictureEvidence(subtitle.View());
			Assert::IsTrue(textEvidence.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::LEFT_RIGHT);
			const auto occupied=EvaluateP010ActivePicturePresentationRetention(subtitle.View(),base);
			Assert::IsTrue(occupied.outwardVisibleBoundsAvailable);
			const auto text=AlphaSourceCrop::ResolvePresentationObservation(base,textEvidence,occupied);
			Assert::AreEqual(base.top,text.bounds.top);
			Assert::IsTrue(text.bounds.bottom>=460 && text.bounds.bottom<540);
			const auto wideEvidence=ExtractP010ActivePictureEvidence(expanded.View());
			const auto wide=AlphaSourceCrop::ResolvePresentationObservation(base,wideEvidence,
				EvaluateP010ActivePicturePresentationRetention(expanded.View(),base));
			Assert::AreEqual(0,wide.bounds.left); Assert::AreEqual(0,wide.bounds.top);
			Assert::AreEqual(960,wide.bounds.right); Assert::AreEqual(540,wide.bounds.bottom);
		}

		TEST_METHOD(AsymmetricBlackMarginsCannotPromoteAllSidedInsetThroughRealPixelExtraction)
		{
			P010Frame scope(960,540), nested(960,540), partial(960,540);
			scope.BlackOutside(0,58,960,482);
			nested.BlackOutside(48,88,912,452);
			partial.BlackOutside(48,88,912,460);
			const auto base=ExtractP010ActivePictureEvidence(scope.View()).trustedBounds;
			const auto partialEvidence=ExtractP010ActivePictureEvidence(partial.View());
			Assert::IsTrue(partialEvidence.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::LEFT_RIGHT);
			Assert::IsTrue(partialEvidence.top.trusted && partialEvidence.bottom.trusted);
			ActivePictureTransitionModel model;
			uint64_t published=0;
			for (uint64_t seq=1;seq<=120;++seq)
			{
				const bool paused=seq>=77 && seq<=88;
				const auto source=seq<=4 ? scope.P010Source() : paused ? partial.P010Source() : nested.P010Source();
				AlphaSourceCrop::TransitionAdmissionInput input;
				input.evidence=ExtractActivePictureEvidence(source);
				input.retention=EvaluateActivePicturePresentationRetention(source,base);
				input.trustedGeometry=input.presentationBeforeObservation=base;
				input.trustedGeometryAvailable=input.compatiblePresentation=true;
				input.trustedGeneration=input.sourceGeneration=1;
				input.sourceSequence=seq; input.framesPerSecond=24;
				input.outwardCandidate=input.evidence.trustedBounds;
				const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(input);
				if (paused)
				{
					Assert::IsTrue(admission.deferPartialComposition && admission.observation.transitionDeferred);
					ActivePictureTransitionDecision queued;
					queued.publish=queued.stable=true;
					queued.bounds=ExtractP010ActivePictureEvidence(nested.View()).trustedBounds;
					Assert::IsFalse(model.AdoptPublishedDecision(queued,
						ActivePictureClassification::BAR_CROP_TRUSTED,admission.observation.transitionDeferred));
				}
				const auto d=model.Observe(admission.observation);
				if (paused) Assert::IsFalse(d.publish);
				if (d.publish && seq>4) { published=seq; break; }
			}
			Assert::AreEqual(uint64_t(0),published);
		}

		TEST_METHOD(RetentionHandoffNeverRelabelsOldSafePixelsAsNewCropProof)
		{
			P010Frame frame(1920,1080);
			frame.BlackOutside(0,100,1920,980);
			const ActivePictureBounds oldBase{0,100,1920,980,1920,1080,2.18,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			auto newBase=oldBase; newBase.top=140; newBase.bottom=940;
			const auto view=frame.View();
			AnalysisLumaSource source;
			source.data=view.data; source.dataBytes=view.dataBytes;
			source.width=view.width; source.height=view.height;
			source.rowBytes=view.lumaPitchBytes; source.chromaRowBytes=view.chromaPitchBytes;
			source.format=AnalysisLumaFormat::P010;
			const auto oldProof=EvaluateActivePicturePresentationRetention(source,oldBase);
			Assert::IsTrue(oldProof.excludedBandsPixelSafe);
			const auto reused=ResolveActivePictureRetentionHandoff(source,oldBase,oldProof,oldBase);
			Assert::IsFalse(reused.refreshed);
			const auto changed=ResolveActivePictureRetentionHandoff(source,oldBase,oldProof,newBase);
			Assert::IsTrue(changed.refreshed && changed.evidence.analysisValid);
			Assert::IsFalse(changed.evidence.excludedBandsPixelSafe);
			Assert::IsFalse(changed.evidence.excludedVerticalBandsPixelSafe);
			Assert::IsTrue(changed.evidence.outwardVisibleBoundsAvailable);
			Assert::IsTrue(changed.evidence.outwardVisibleBounds.top<=oldBase.top);
			source.data=nullptr;
			const auto invalid=ResolveActivePictureRetentionHandoff(source,oldBase,oldProof,newBase);
			Assert::IsFalse(invalid.evidence.analysisValid);
		}

		TEST_METHOD(PublishedGeometryGetsSameFramePixelEvidenceBeforeCropEvaluation)
		{
			using namespace AlphaSourceCrop;
			P010Frame frame(3840,2160);
			frame.BlackOutside(192,440,3648,1720);
			const ActivePictureBounds oldBase{192,372,3648,1788,3840,2160,2.44,ActivePictureBounds::BarAxes::BOTH};
			const ActivePictureBounds newBase{192,440,3648,1720,3840,2160,2.7,ActivePictureBounds::BarAxes::BOTH};
			for (bool outside : {false,true})
			{
				if(outside) frame.FillRectangle(3648,700,3670,1300,400);
				const auto view=frame.View();
				AnalysisLumaSource source;
				source.data=view.data; source.dataBytes=view.dataBytes;
				source.width=view.width; source.height=view.height;
				source.rowBytes=view.lumaPitchBytes; source.chromaRowBytes=view.chromaPitchBytes;
				source.format=AnalysisLumaFormat::P010;
				const auto before=EvaluateActivePicturePresentationRetention(source,oldBase);
				const auto handoff=ResolveActivePictureRetentionHandoff(source,oldBase,before,newBase);
				Assert::IsTrue(handoff.refreshed);
				Assert::AreEqual(newBase.top,handoff.bounds.top);
				Assert::AreEqual(!outside,handoff.evidence.excludedBandsPixelSafe);
				Input crop;
				crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
				crop.classification=crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.geometry=newBase; crop.geometrySourceGeneration=crop.frameSourceGeneration=1;
				crop.frameSourceSequence=4005; crop.rasterWidth=3840; crop.rasterHeight=2160;
				if(outside) {
					crop.barCropRefinementHorizontalConflict=true;
					crop.currentVisibleBoundsAvailable=handoff.evidence.outwardVisibleBoundsAvailable;
					crop.currentVisibleBase=handoff.bounds;
					crop.currentVisibleBounds=handoff.evidence.outwardVisibleBounds;
					crop.currentVisibleSourceSequence=4005; crop.currentVisibleSourceGeneration=1;
					crop.outwardExpansion=newBase; crop.outwardExpansion.right=3720;
					crop.outwardExpansionAvailable=crop.outwardPresentationActive=true; crop.outwardExpansionSourceGeneration=1;
				}
				PresentationRecoveryInput ri; ri.crop=crop; ri.candidate=Evaluate(crop);
				const auto d=EvaluatePresentationRecovery(ri);
				Assert::IsTrue(d.presentation.applyCrop);
				Assert::IsFalse(d.started);
			}
		}

		TEST_METHOD(SubtitleAndSparseStarsCannotCertifyAnExpandedPictureStrip)
		{
			const ActivePictureBounds scope{192,372,3648,1788,3840,2160,2.44,ActivePictureBounds::BarAxes::BOTH};
			for (bool stars : {false,true}) {
				P010Frame frame(3840,2160);
				frame.BlackOutside(scope.left,scope.top,scope.right,scope.bottom);
				if(stars) {
					for(int y=24; y<2160; y+=80)
						frame.FillRectangle(1920,y,1926,y+4,700);
				} else frame.FillRectangle(1200,1830,2640,1850,700);
				const auto evidence=EvaluateP010ActivePicturePresentationRetention(frame.View(),scope);
				Assert::IsTrue(evidence.excludedHorizontalBandsPixelSafe);
				auto fullHeight=scope; fullHeight.top=0; fullHeight.bottom=2160;
				Assert::IsFalse(AlphaSourceCrop::ConfirmOutwardPictureTransition({},scope,fullHeight,evidence,7,1).authoritative);
				Assert::IsFalse(AlphaSourceCrop::ConfirmOutwardPictureTransition({},scope,fullHeight,evidence,7,1).broadOpposingPicture);
			}
		}

		TEST_METHOD(GradualPictureExpansionUsesNewStripRatherThanWholeOldBar)
		{
			P010Frame frame(3840,2160);
			frame.BlackOutside(0,208,3840,1952);
			const ActivePictureBounds oldCrop{40,244,3800,1912,3840,2160,2.25,ActivePictureBounds::BarAxes::BOTH};
			const auto evidence = EvaluateP010ActivePicturePresentationRetention(frame.View(),oldCrop);
			Assert::IsTrue(evidence.activePicture.available);
			Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),int(evidence.activePicture.classification));
			AlphaSourceCrop::OutwardPictureConfirmationState state;
			for (uint64_t seq=323; seq<=325; ++seq) {
				const auto d = AlphaSourceCrop::ConfirmOutwardPictureTransition(state,oldCrop,
					evidence.activePicture.trustedBounds,evidence,2,seq);
				Assert::IsTrue(d.broadOpposingPicture);
				Assert::AreEqual(seq==325,d.authoritative);
				state=d.state;
			}
		}

		TEST_METHOD(InternalDividerDoesNotBecomeAnOuterCropEdge)
		{
			for (bool subtitle : {false, true})
			{
				P010Frame frame(1920,1080);
				frame.BlackOutside(96,182,1824,898);
				frame.FillRectangle(948,182,972,898,64);
				if (subtitle)
					for (int x=640; x<1280; x+=24)
						frame.FillRectangle(x,910,x+12,930,900);
				const auto evidence = ExtractP010ActivePictureEvidence(frame.View());
				Assert::IsTrue(evidence.available);
				const auto bounds = evidence.classification == ActivePictureClassification::PROVISIONAL
					? evidence.proposedBounds : evidence.trustedBounds;
				Assert::IsTrue(bounds.left < 200 && bounds.right > 1720,
					L"An internal divider must not discard either panel");
			}
		}

		TEST_METHOD(SparseFullHeightStarsCannotAcquireAnInsetCropAtStartup)
		{
			for (int phase = 0; phase < 24; ++phase)
			{
				P010Frame frame(640, 360);
				frame.Fill(64, 512, 512);
				for (int point = 0; point < 32; ++point)
				{
					const int x = (point * 83 + phase * 9) % 640;
					const int y = (point * 47 + phase * 5) % 360;
					frame.FillRectangle(x, y, x + 1, y + 1, 900);
				}
				const auto darkness = EvaluateP010ActivePictureGlobalNearBlack(frame.View());
				Assert::IsTrue(darkness.nearBlack);
				const auto observed = ConstrainNearBlackCropAcquisition(
					ExtractP010ActivePictureEvidence(frame.View()), darkness.nearBlack);
				Assert::IsTrue(observed.classification != ActivePictureClassification::BAR_CROP_TRUSTED);
				AlphaSourceCrop::Input input;
				input.automaticCropEnabled = true;
				input.latestObservationClassification = observed.classification;
				input.rasterWidth = 640; input.rasterHeight = 360;
				const auto decision = AlphaSourceCrop::Evaluate(input);
				Assert::IsFalse(decision.applyCrop);
				Assert::AreEqual(0, decision.sourceBounds.top);
				Assert::AreEqual(360, decision.sourceBounds.bottom);
			}
		}

		TEST_METHOD(MovingSparseStarsInsideKnownLetterboxNeverChangePresentation)
		{
			const auto scope = ScopePresentation(640, 360, 44, 316);
			for (int phase = 0; phase < 32; ++phase)
			{
				P010Frame frame(640, 360);
				frame.Fill(64, 512, 512);
				for (int point = 0; point < 24; ++point)
				{
					const int x = (point * 79 + phase * 7) % 636;
					const int y = 48 + (point * 29 + phase * 3) % 260;
					frame.FillRectangle(x, y, x + 2, y + 2, 900);
				}
				const auto evidence = EvaluateP010ActivePicturePresentationRetention(frame.View(), scope);
				Assert::IsTrue(evidence.currentlyPixelSafe);
				AlphaSourceCrop::Input input;
				input.automaticCropEnabled = input.sharedGeometryAvailable = true;
				input.latestObservationIsProvisional = true;
				input.frameLocalPresentationRetentionEvaluated = true;
				input.frameLocalPresentationRetentionSafe = evidence.currentlyPixelSafe;
				input.geometry = scope;
				input.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
				input.geometrySourceGeneration = input.frameSourceGeneration = 1;
				input.rasterWidth = 640; input.rasterHeight = 360;
				const auto decision = AlphaSourceCrop::Evaluate(input);
				Assert::IsTrue(decision.applyCrop);
				Assert::AreEqual(44, decision.sourceBounds.top);
				Assert::AreEqual(316, decision.sourceBounds.bottom);
			}
		}

		TEST_METHOD(OutsideStarsWithdrawImmediatelyAndLetterboxReturnNeedsFreshProof)
		{
			using namespace AlphaSourceCrop;
			const auto scope = ScopePresentation(640, 360, 44, 316);
			PresentationRecoveryInput input;
			input.crop.automaticCropEnabled = input.crop.sharedGeometryAvailable = true;
			input.crop.latestObservationIsProvisional = true;
			input.crop.geometry = scope;
			input.crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.crop.geometrySourceGeneration = input.crop.frameSourceGeneration = 1;
			input.crop.rasterWidth = 640; input.crop.rasterHeight = 360;
			input.retentionSourceGeneration = 1;
			input.framesPerSecond = 24;
			for (uint64_t sequence = 1; sequence <= 39; ++sequence)
			{
				P010Frame frame(640, 360);
				frame.BlackOutside(0, 44, 640, 316);
				if (sequence <= 32)
				{
					// Move one small bright cluster across line-grid phases. It is
					// outside the saved crop, so fixture truth requires full raster.
					const int x = 17 + static_cast<int>(sequence) * 7;
					frame.FillRectangle(x, 10, x + 10, 20, 900);
				}
				const auto evidence = EvaluateP010ActivePicturePresentationRetention(frame.View(), scope);
				input.crop.frameSourceSequence = input.retentionSourceSequence = sequence;
				input.crop.frameLocalPresentationRetentionEvaluated = evidence.analysisValid;
				input.crop.frameLocalPresentationRetentionSafe = evidence.currentlyPixelSafe;
				input.measurementCurrent = true;
				input.retentionEvaluated = evidence.analysisValid && evidence.presentationValid;
				input.retentionBounds = scope;
				input.excludedBandsPixelSafe = evidence.excludedBandsPixelSafe;
				input.nearBlackEvaluated = true;
				input.globalNearBlack = evidence.globalNearBlack;
				input.observationAvailable = evidence.proposedBoundsAvailable;
				input.observation = evidence.activePicture.proposedBounds;
				input.candidate = Evaluate(input.crop);
				const auto decision = EvaluatePresentationRecovery(input);
				if (sequence <= 32) Assert::IsFalse(evidence.excludedBandsPixelSafe);
				Assert::AreEqual(sequence == 39, decision.presentation.applyCrop);
				Assert::AreEqual(sequence == 39 ? 44 : 0, decision.presentation.sourceBounds.top);
				Assert::AreEqual(sequence == 39 ? 316 : 360, decision.presentation.sourceBounds.bottom);
				input.previous = decision.state;
			}
		}

		TEST_METHOD(GeneratedFramesCertifyExactInwardProofAndNearBlackVeto)
		{
			auto observation = [](
				uint64_t frameNumber,
				const ActivePictureEvidence& evidence)
			{
				ActivePictureObservation value;
				value.frameNumber = frameNumber;
				value.available = evidence.available;
				value.classification = evidence.classification;
				value.bounds = evidence.classification ==
					ActivePictureClassification::BAR_CROP_TRUSTED
					? evidence.trustedBounds : evidence.proposedBounds;
				return value;
			};
			auto identity = [](uint64_t sequence, uint64_t frameNumber)
			{
				return ActivePictureFrameIdentity{
					61, sequence, frameNumber, frameNumber * 1000 };
			};

			P010Frame shallow(320, 180);
			shallow.BlackOutside(0, 8, 320, 172);
			P010Frame deep(320, 180);
			deep.BlackOutside(0, 22, 320, 158);
			const ActivePictureEvidence shallowEvidence =
				ExtractActivePictureEvidence(shallow.P010Source());
			const ActivePictureEvidence deepEvidence =
				ExtractActivePictureEvidence(deep.P010Source());
			const ActivePictureGlobalNearBlackEvidence deepNearBlack =
				EvaluateActivePictureGlobalNearBlack(deep.P010Source());
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(shallowEvidence.classification));
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(deepEvidence.classification));
			Assert::IsTrue(deepNearBlack.evaluated);
			Assert::IsFalse(deepNearBlack.nearBlack);

			ActivePictureDecisionTimeline timeline;
			timeline.Reset(61);
			ActivePictureFrameDecision published;
			uint64_t sequence = 1;
			for (uint64_t frame = 1;
				frame <= ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++frame)
			{
				const bool didPublish = timeline.SubmitScheduledObservation(
					identity(sequence++, frame),
					observation(frame, shallowEvidence), 0, 0, published);
				Assert::AreEqual(
					frame == ActivePictureTransitionModel::INITIAL_CONFIRMATIONS,
					didPublish);
			}
			const auto candidate = identity(sequence++, 602);
			const auto skipped = identity(sequence++, 603);
			const auto confirmation = identity(sequence++, 604);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			for (const auto& current : { candidate, skipped, confirmation })
			{
				Assert::IsTrue(timeline.TrackLookaheadEvidence(
					current,
					observation(current.sourceFrameNumber, deepEvidence),
					deepNearBlack.evaluated, deepNearBlack.nearBlack));
			}
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, observation(602, deepEvidence), 5, 4, published));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, observation(604, deepEvidence), 5, 4, published));
			Assert::AreEqual(candidate.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual(static_cast<int>(
				ActivePictureDecisionAssociation::EXACT_INWARD),
				static_cast<int>(published.association));

			P010Frame sparseTitle(320, 180);
			sparseTitle.Fill(90, 512, 512);
			sparseTitle.BlackOutside(0, 22, 320, 158);
			sparseTitle.FillRectangle(148, 84, 172, 96, 700);
			const ActivePictureEvidence titleEvidence =
				ExtractActivePictureEvidence(sparseTitle.P010Source());
			const ActivePictureGlobalNearBlackEvidence titleNearBlack =
				EvaluateActivePictureGlobalNearBlack(sparseTitle.P010Source());
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(titleEvidence.classification));
			Assert::AreEqual(deepEvidence.trustedBounds.top,
				titleEvidence.trustedBounds.top);
			Assert::AreEqual(deepEvidence.trustedBounds.bottom,
				titleEvidence.trustedBounds.bottom);
			Assert::IsTrue(titleNearBlack.evaluated);
			Assert::IsTrue(titleNearBlack.nearBlack);

			ActivePictureDecisionTimeline veto;
			veto.Reset(61);
			sequence = 1;
			for (uint64_t frame = 1;
				frame <= ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++frame)
			{
				veto.SubmitScheduledObservation(identity(sequence++, frame),
					observation(frame, shallowEvidence), 0, 0, published);
			}
			const auto vetoCandidate = identity(sequence++, 702);
			const auto vetoSkipped = identity(sequence++, 703);
			const auto vetoConfirmation = identity(sequence++, 704);
			Assert::IsTrue(veto.TrackAcceptedFrame(vetoCandidate));
			Assert::IsTrue(veto.TrackAcceptedFrame(vetoSkipped));
			Assert::IsTrue(veto.TrackAcceptedFrame(vetoConfirmation));
			Assert::IsTrue(veto.TrackLookaheadEvidence(vetoCandidate,
				observation(702, deepEvidence), true, false));
			Assert::IsFalse(veto.SubmitScheduledObservation(vetoCandidate,
				observation(702, deepEvidence), 5, 4, published));
			Assert::IsTrue(veto.TrackLookaheadEvidence(vetoSkipped,
				observation(703, titleEvidence),
				titleNearBlack.evaluated, titleNearBlack.nearBlack));
			Assert::IsTrue(veto.TrackLookaheadEvidence(vetoConfirmation,
				observation(704, deepEvidence), true, false));
			Assert::IsTrue(veto.SubmitScheduledObservation(vetoConfirmation,
				observation(704, deepEvidence), 5, 4, published));
			Assert::AreEqual(static_cast<int>(
				ActivePictureInwardProofValidation::EVIDENCE_NEAR_BLACK),
				static_cast<int>(published.inwardProof));
			Assert::AreEqual(vetoConfirmation.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
		}

		TEST_METHOD(FullRasterIsTrustedImmediately)
		{
			P010Frame frame(320, 180, 16);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::FULL_RASTER_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::AreEqual(0, evidence.trustedBounds.left);
			Assert::AreEqual(180, evidence.trustedBounds.bottom);
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::NONE),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(GeneratedFramesGateOutwardLogicalAspectOnBroadOpposingPicture)
		{
			using namespace AlphaSourceCrop;
			const ActivePictureBounds scope =
				ScopePresentation(320, 180, 22, 158);
			const ActivePictureBounds full = {
				0, 0, 320, 180, 320, 180, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE };

			P010Frame localized(320, 180);
			localized.BlackOutside(0, 22, 320, 158);
			localized.FillRectangle(120, 8, 136, 18, 600);
			localized.FillRectangle(184, 162, 200, 172, 600);
			const auto localizedRetention =
				EvaluateActivePicturePresentationRetention(
					localized.P010Source(), scope);
			OutwardPictureConfirmationState state;
			for (int sample = 0; sample < 6; ++sample)
			{
				const auto decision = ConfirmOutwardPictureTransition(
					state, scope, full, localizedRetention, 3);
				state = decision.state;
				Assert::IsFalse(decision.authoritative);
				Assert::AreEqual(0U, state.confirmations);
			}

			P010Frame broad(320, 180);
			const auto broadRetention =
				EvaluateActivePicturePresentationRetention(
					broad.P010Source(), scope);
			for (uint32_t sample = 1;
				sample <= OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED; ++sample)
			{
				const auto decision = ConfirmOutwardPictureTransition(
					state, scope, full, broadRetention, 3);
				state = decision.state;
				Assert::AreEqual(sample, state.confirmations);
				Assert::AreEqual(
					sample == OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED,
					decision.authoritative);
			}

			// Any intervening localized frame breaks consecutiveness.
			state = ConfirmOutwardPictureTransition(state, scope, full,
				localizedRetention, 3).state;
			Assert::AreEqual(0U, state.confirmations);
			state = ConfirmOutwardPictureTransition(state, scope, full,
				broadRetention, 3).state;
			Assert::AreEqual(1U, state.confirmations);

			const ActivePictureBounds pillar = {
				40, 0, 280, 180, 320, 180, 4.0 / 3.0,
				ActivePictureBounds::BarAxes::LEFT_RIGHT };
			const auto horizontalRetention =
				EvaluateActivePicturePresentationRetention(
					broad.P010Source(), pillar);
			const auto horizontal = ConfirmOutwardPictureTransition(
				{}, pillar, full, horizontalRetention, 3);
			Assert::IsTrue(horizontal.broadOpposingPicture);
			Assert::AreEqual(1U, horizontal.state.confirmations);
		}

		TEST_METHOD(ScopeBarsHaveTrustedOpposingLumaAndChromaEvidence)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::IsTrue(evidence.trustedBounds.top >= 20);
			Assert::IsTrue(evidence.trustedBounds.bottom <= 160);
			Assert::IsTrue(evidence.lumaSamples < 30000);
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::TOP_BOTTOM),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(StartupBottomSubtitleRecoversSymmetricScopeHypothesis)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(110, 162, 210, 172, 700);
			// Sparse colored receiver text in the other bar must not defeat the
			// clean boundary consensus.
			frame.FillRectangle(12, 8, 28, 14, 500, 300, 700);
			const AnalysisLumaSource source = frame.P010Source();
			const auto observed = ExtractActivePictureEvidence(source);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::PROVISIONAL),
				static_cast<int>(observed.classification));
			Assert::IsTrue(observed.top.trusted, L"top edge must remain clean");
			Assert::IsTrue(observed.proposedBounds.top >
				180 - observed.proposedBounds.bottom);

			const auto hypothesis = EvaluateSymmetricVerticalBarHypothesis(
				source, observed);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(hypothesis.classification));
			Assert::AreEqual(hypothesis.trustedBounds.top,
				180 - hypothesis.trustedBounds.bottom);
			Assert::IsTrue(hypothesis.trustedBounds.top >= 20);
			Assert::IsTrue(hypothesis.trustedBounds.bottom <= 160);
		}

		TEST_METHOD(StartupHypothesisRejectsBroadOneSidedPictureExpansion)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(0, 158, 320, 174, 300);
			const AnalysisLumaSource source = frame.P010Source();
			const auto observed = ExtractActivePictureEvidence(source);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::PROVISIONAL),
				static_cast<int>(observed.classification));

			const auto hypothesis = EvaluateSymmetricVerticalBarHypothesis(
				source, observed);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::PROVISIONAL),
				static_cast<int>(hypothesis.classification));
		}

		TEST_METHOD(DarkCinematicScopeFrameKeepsTrustedBars)
		{
			P010Frame frame(3840, 2160);
			// Model a low-key movie shot: most of the picture sits only slightly
			// above encoded black, with uneven shadow color and a few localized
			// practical lights. The bars themselves remain neutral and coherent.
			frame.Fill(74, 476, 548);
			frame.BlackOutside(0, 280, 3840, 1880);
			frame.FillRectangle(240, 280, 1008, 1880, 96, 500, 524);
			frame.FillRectangle(1512, 280, 2280, 1880, 112, 520, 504);
			frame.FillRectangle(2784, 280, 3504, 1880, 92, 488, 536);
			frame.FillRectangle(1752, 576, 2088, 1032, 640, 512, 512);

			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::AreEqual(280,evidence.proposedBounds.top);
            Assert::AreEqual(276, evidence.trustedBounds.top);
			Assert::AreEqual(1880,evidence.proposedBounds.bottom);
            Assert::AreEqual(1884, evidence.trustedBounds.bottom);
			Assert::IsTrue(evidence.axisEvidence.HasVerifiedVerticalCropProfile(evidence.trustedBounds));
            const size_t profiles=evidence.verticalBarProfile.samples+evidence.verticalBarGuardProfile.samples;
            Assert::IsTrue(evidence.lumaSamples>=profiles);
            Assert::IsTrue(evidence.lumaSamples-profiles<30000,
                L"The original bounded rough inspection remains bounded independently of strict profiles.");
            for(const auto* profile:{&evidence.verticalBarProfile,&evidence.verticalBarGuardProfile})
            {
                const int step=2160/540;
                const int topDepth=profile->apertureTop,bottomDepth=2160-profile->apertureBottom;
                const size_t budget=size_t(96)*((topDepth+step-1)/step+(bottomDepth+step-1)/step+48);
                Assert::IsTrue(profile->evaluated && profile->completed && profile->clean);
                Assert::IsTrue(profile->samples>0 && profile->samples<=budget);
            }
		}

		TEST_METHOD(FourByThreePillarboxIsTrusted)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(40, 0, 280, 180);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.left.trusted);
			Assert::IsTrue(evidence.right.trusted);
			Assert::IsTrue(evidence.trustedBounds.left >= 38);
			Assert::IsTrue(evidence.trustedBounds.right <= 282);
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::LEFT_RIGHT),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(WindowboxCarriesIndependentAuthorityForBothAxes)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(40, 22, 280, 158);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::BOTH),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(SourceBakedTopControlExpandsTheMeasuredScopeEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(0, 8, 320, 18, 600);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::IsTrue(evidence.proposedBounds.top <= 10);
			Assert::IsTrue(evidence.proposedBounds.top < 22);
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(SourceBakedSideControlExpandsTheMeasuredPillarEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(40, 0, 280, 180);
			frame.FillRectangle(10, 0, 30, 180, 600);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::IsTrue(evidence.proposedBounds.left <= 12);
			Assert::IsTrue(evidence.proposedBounds.left < 40);
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(TrustedVerticalBarsIgnoreUntrustedHorizontalArtwork)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			// A dark one-sided feature spans the active picture. It can look
			// like a left bar, but has no trusted opposing right boundary.
			frame.FillRectangle(0, 22, 20, 158, 64);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::IsFalse(
				evidence.left.trusted && evidence.right.trusted);
			Assert::AreNotEqual(0, evidence.proposedBounds.left);
			Assert::AreEqual(0, evidence.trustedBounds.left);
			Assert::AreEqual(320, evidence.trustedBounds.right);
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::TOP_BOTTOM),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(SmallImaxStyleBarsRequireAndPassBoundaryEvidence)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 6, 320, 174);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(AsymmetricOrColoredEdgeCannotAuthorizeCrop)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 150, 64, 400, 620);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(DarkArtworkDoesNotBecomeTrustedBars)
		{
			P010Frame frame(320, 180);
			frame.Fill(80, 430, 590);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(RejectsShortPlaneAndInvalidPitchWithoutReadingPastBounds)
		{
			P010Frame frame(320, 180, 16);
			auto shortView = frame.View(1);
			auto evidence = ExtractP010ActivePictureEvidence(shortView);
			Assert::IsFalse(evidence.available);
			auto badPitch = frame.View();
			badPitch.lumaPitchBytes = 100;
			evidence = ExtractP010ActivePictureEvidence(badPitch);
			Assert::IsFalse(evidence.available);
		}

		TEST_METHOD(AdversarialBlackFrameStaysInsideFixedLumaBudget)
		{
			P010Frame frame(3840, 2160);
			frame.Fill(64, 512, 512);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.lumaSamples < 30000);
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

        TEST_METHOD(ContainedInspectionResolvesLatchButKeepsNormalSevenFrameRecovery)
        {
            using namespace AlphaSourceCrop;
            auto fixture=MakeContainedInspectionFixture();
            auto& input=fixture.recovery;
            const auto before=EvaluatePresentationRecovery(input);
            Assert::IsTrue(before.state.active && !before.released);
            Assert::AreEqual(0u,before.samples,L"The original circular latch prevents even the first contained-frame proof vote");
            Assert::IsTrue((before.gates & RECOVERY_OWNER)!=0);
            Assert::IsTrue(CanResolveContainedInspectionRetention(fixture.inspection,input),
                L"Fresh contained safe pixels must clear only this admitted recovery contract's obsolete inspection latch");
            for (uint64_t sequence=107;sequence<114;++sequence)
            {
                input.crop.frameSourceSequence=input.retentionSourceSequence=sequence;
                VerticalInspectionBridgeInput inspect;
                inspect.previous=fixture.inspection;
                inspect.sourceGeneration=1; inspect.presentationEpoch=4;
                inspect.sourceSequence=sequence; inspect.trustedBase=input.crop.geometry;
                inspect.containedRetentionResolved=CanResolveContainedInspectionRetention(fixture.inspection,input);
                const auto bridge=UpdateVerticalInspectionBridge(inspect);
                Assert::IsFalse(bridge.state.failOpenLatched);
                fixture.inspection=bridge.state;
                input.crop.presentationFailOpen=bridge.state.failOpenLatched;
                input.candidate=Evaluate(input.crop);
                const auto result=EvaluatePresentationRecovery(input);
                Assert::IsFalse(result.samplingReaffirmed,L"Contained evidence must not borrow a sampling/native shortcut");
                Assert::AreEqual(unsigned(sequence-106),result.samples);
                Assert::AreEqual(sequence==113,result.released);
                Assert::IsFalse(input.crop.latestObservationSupportsCrop);
                const auto shown=AdmitCropPresentation(input.previousAdmission,input.crop,result.presentation,4);
                Assert::IsFalse(shown.blocked);
                Assert::IsTrue(shown.presentation.applyCrop);
                Assert::AreEqual(sequence==113 ? 276 : 34,shown.presentation.sourceBounds.top);
                Assert::AreEqual(sequence==113 ? 1884 : 2126,shown.presentation.sourceBounds.bottom);
                input.previous=result.state; input.previousAdmission=shown.state;
            }
        }

        TEST_METHOD(ContainedInspectionUnsafeInterruptionRestartsAllSevenRecoveryVotes)
        {
            using namespace AlphaSourceCrop;
            auto fixture=MakeContainedInspectionFixture();
            auto& input=fixture.recovery;
            VerticalInspectionBridgeInput inspect;
            inspect.previous=fixture.inspection; inspect.sourceGeneration=1;
            inspect.presentationEpoch=4; inspect.sourceSequence=input.crop.frameSourceSequence;
            inspect.trustedBase=input.crop.geometry;
            inspect.containedRetentionResolved=CanResolveContainedInspectionRetention(fixture.inspection,input);
            Assert::IsTrue(inspect.containedRetentionResolved);
            const auto cleared=UpdateVerticalInspectionBridge(inspect);
            Assert::IsFalse(cleared.state.active || cleared.state.failOpenLatched);
            input.crop.presentationFailOpen=cleared.state.failOpenLatched;
            P010Frame safe(3840,2160), occupied(3840,2160);
            safe.BlackOutside(788,320,3840,1884);
            occupied.BlackOutside(788,320,3840,1884);
            occupied.FillRectangle(900,80,1500,120,300);
            for (uint64_t sequence=107;sequence<=117;++sequence)
            {
                const bool interrupted=sequence==110;
                const auto r=EvaluateActivePicturePresentationRetention(
                    interrupted ? occupied.P010Source() : safe.P010Source(),input.crop.geometry);
                Assert::AreEqual(!interrupted,r.CanRetainPresentation());
                input.crop.frameSourceSequence=input.retentionSourceSequence=sequence;
                input.crop.frameLocalPresentationRetentionSafe=r.CanRetainPresentation();
                input.crop.latestObservationClassification=input.observationClassification=r.activePicture.classification;
                input.crop.latestObservationIsProvisional=r.activePicture.classification==ActivePictureClassification::PROVISIONAL;
                input.observationAvailable=r.activePicture.available;
                input.observation=r.activePicture.proposedBounds;
                input.excludedBandsPixelSafe=r.excludedBandsPixelSafe;
                input.globalNearBlack=r.globalNearBlack;
                input.crop.currentVisibleBoundsAvailable=r.outwardVisibleBoundsAvailable;
                input.crop.currentVisibleBounds=r.outwardVisibleBounds;
                input.crop.currentVisibleBase=input.crop.geometry;
                input.crop.currentVisibleSourceGeneration=1;
                input.crop.currentVisibleSourceSequence=sequence;
                if(interrupted) Assert::IsFalse(CanResolveContainedInspectionRetention(fixture.inspection,input));
                input.candidate=Evaluate(input.crop);
                const auto result=EvaluatePresentationRecovery(input);
                const unsigned expected=interrupted ? 0 : unsigned(sequence<110 ? sequence-106 : sequence-110);
                Assert::AreEqual(expected,result.samples,L"Unsafe pixels must erase accumulated votes, not merely pause proof");
                Assert::AreEqual(sequence==117,result.released);
                if(interrupted) Assert::IsTrue(result.proofReset);
                Assert::IsFalse(result.samplingReaffirmed);
                const auto shown=AdmitCropPresentation(input.previousAdmission,input.crop,result.presentation,4);
                Assert::IsTrue(shown.presentation.applyCrop && !shown.blocked);
                Assert::AreEqual(sequence==117 ? 276 : 34,shown.presentation.sourceBounds.top);
                Assert::AreEqual(sequence==117 ? 1884 : 2126,shown.presentation.sourceBounds.bottom);
                input.previous=result.state; input.previousAdmission=shown.state;
            }
        }
        TEST_METHOD(ContainedInspectionRejectsUnknownStaleUnsafeAndCompetingContracts)
        {
            using namespace AlphaSourceCrop;
            const auto fixture=MakeContainedInspectionFixture();
            for (int fault=0;fault<48;++fault)
            {
                auto inspection=fixture.inspection;
                auto input=fixture.recovery;
                switch(fault)
                {
                case 0: inspection.active=false; break;
                case 1: inspection.failOpenLatched=false; break;
                case 2: inspection.sourceGeneration=2; break;
                case 3: inspection.presentationEpoch=5; break;
                case 4: inspection.trustedBase.top+=4; break;
                case 5: input.previous.active=false; break;
                case 6: input.previous.sourceGeneration=2; break;
                case 7: input.previous.presentationEpoch=5; break;
                case 8: input.previous.trustedCrop.top+=4; break;
                case 9: input.previousAdmission.available=false; break;
                case 10: input.previousAdmission.outwardPresentationAvailable=false; break;
                case 11: input.previousAdmission.sourceGeneration=2; break;
                case 12: input.previousAdmission.presentationEpoch=5; break;
                case 13: input.previousAdmission.trustedCrop.top+=4; break;
                case 14: --input.previousAdmission.presentationSourceSequence; break;
                case 15: input.previousAdmission.presentationSourceSequence=input.crop.frameSourceSequence; break;
                case 16: input.previous.lastSourceSequence=input.crop.frameSourceSequence; break;
                case 17: input.cadenceRepeat=true; break;
                case 18: input.measurementCurrent=false; break;
                case 19: input.retentionEvaluated=false; break;
                case 20: input.retentionSourceGeneration=2; break;
                case 21: --input.retentionSourceSequence; break;
                case 22: input.retentionBounds.top+=4; break;
                case 23: input.excludedBandsPixelSafe=false; break;
                case 24: input.crop.latestObservationIsProvisional=false; break;
                case 25: input.crop.latestObservationIsUnavailable=true; break;
                case 26: input.observationClassification=ActivePictureClassification::FULL_RASTER_TRUSTED; break;
                case 27: input.observation.bottom+=4; break;
                case 28: input.observation.left=-4; break;
                case 29: input.crop.fullRasterPresentationAuthoritative=true; break;
                case 30: input.crop.movingPictureTransition=true; break;
                case 31: input.crop.nearBlackEpisodeFullRaster=true; break;
                case 32: input.nearBlackEvaluated=false; break;
                case 33: input.globalNearBlack=true; break;
                case 34:
                    input.crop.currentVisibleBoundsAvailable=true;
                    input.crop.currentVisibleBase=input.crop.geometry;
                    input.crop.currentVisibleBounds=input.crop.geometry;
                    input.crop.currentVisibleBounds.top=34;
                    input.crop.currentVisibleSourceGeneration=1;
                    input.crop.currentVisibleSourceSequence=input.crop.frameSourceSequence;
                    break;
                case 35: input.crop.frameLocalPresentationRetentionSafe=false; break;
                case 36: input.crop.frameLocalPresentationRetentionEvaluated=false; break;
                case 37:
                    inspection.trustedBase.trustedBarAxes=input.crop.geometry.trustedBarAxes=
                        input.previous.trustedCrop.trustedBarAxes=input.previousAdmission.trustedCrop.trustedBarAxes=
                        input.retentionBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
                    break;
                case 38: input.crop.automaticCropEnabled=false; break;
                case 39: input.crop.geometrySourceGeneration=2; break;
                case 40: input.crop.outwardPresentationActive=true; break;
                case 41: input.crop.verticalTranslationActive=true; break;
                case 42: input.crop.verticalFitConfirmationPending=true; break;
                case 43: input.crop.latestObservationSupportsCrop=true; break;
                case 44: input.previousAdmission.outwardPresentation=input.crop.geometry; break;
                case 45: input.previousAdmission.outwardPresentation.top=280; break;
                case 46: input.previousAdmission.outwardPresentation.top=-2; break;
                case 47: input.observationAvailable=false; break;
                }
                const auto message=std::wstring(L"Contained inspection veto case ")+std::to_wstring(fault);
                Assert::IsFalse(CanResolveContainedInspectionRetention(inspection,input),message.c_str());
            }
        }

        TEST_METHOD(ContainedInspectionKeepsCurrentOsdAndGenuinelyTallerPictureVisible)
        {
            using namespace AlphaSourceCrop;
            const auto fixture=MakeContainedInspectionFixture();
            for (bool fullPicture : {false,true})
            {
                P010Frame frame(3840,2160);
                frame.BlackOutside(788,320,3840,1884);
                if (fullPicture) frame.FillRectangle(0,68,3840,2092,300);
                else frame.FillRectangle(900,80,1500,120,300);
                const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),fixture.recovery.crop.geometry);
                Assert::IsFalse(r.currentlyPixelSafe);
                auto input=fixture.recovery;
                input.excludedBandsPixelSafe=r.excludedBandsPixelSafe;
                input.crop.frameLocalPresentationRetentionSafe=r.CanRetainPresentation();
                input.observation=r.activePicture.proposedBounds;
                input.observationAvailable=r.activePicture.available;
                input.observationClassification=r.activePicture.classification;
                input.crop.latestObservationClassification=r.activePicture.classification;
                input.crop.latestObservationIsProvisional=r.activePicture.classification==ActivePictureClassification::PROVISIONAL;
                input.globalNearBlack=r.globalNearBlack;
                input.crop.currentVisibleBoundsAvailable=r.outwardVisibleBoundsAvailable;
                input.crop.currentVisibleBounds=r.outwardVisibleBounds;
                input.crop.currentVisibleBase=input.crop.geometry;
                input.crop.currentVisibleSourceGeneration=1;
                input.crop.currentVisibleSourceSequence=input.crop.frameSourceSequence;
                Assert::IsFalse(CanResolveContainedInspectionRetention(fixture.inspection,input));
                const auto result=EvaluatePresentationRecovery(input);
                Assert::IsFalse(result.released);
                Assert::AreEqual(0u,result.samples);
                Assert::IsTrue(result.presentation.sourceBounds.top<=34 && result.presentation.sourceBounds.bottom>=2126);
            }
        }
        TEST_METHOD(PartialProposalSafeOutwardStripRetainsOnlyEstablishedGeometry)
        {
            // Synthetic pixels model the logged partial-observation geometry;
            // these are not a reconstruction of the Fox/Alien source frame.
            for (int scale : {1,2})
            for (bool p210 : {false,true})
            for (bool above : {false,true})
            {
                const int width=1920*scale, height=1080*scale;
                const int top=142*scale, bottom=938*scale, step=2*scale;
                P010Frame frame(width,height,64,p210);
                frame.BlackOutside(above ? 0 : 486*scale, above ? top : 330*scale,
                    above ? 1434*scale : width, above ? 750*scale : bottom);
                const int stripRow=above ? top-step : bottom+step-1;
                frame.FillRectangle(0,stripRow,width,stripRow+1,96);
                const auto scope=ScopePresentation(width,height,top,bottom);
                const auto r=EvaluateActivePicturePresentationRetention(
                    p210 ? frame.P210Source() : frame.P010Source(),scope);
                Assert::IsTrue(r.activePicture.available && r.proposedBoundsAvailable);
                Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(r.activePicture.classification));
                Assert::IsTrue(r.excludedBandsPixelSafe && !r.globalNearBlack);
                Assert::IsFalse(r.proposedBoundsContained || r.outwardVisibleBoundsAvailable);
                const auto& observed=r.activePicture.proposedBounds;
                Assert::AreEqual(above ? top-step : 330*scale,observed.top);
                Assert::AreEqual(above ? 750*scale : bottom+step,observed.bottom);
                Assert::IsTrue(above ? observed.right<scope.right : observed.left>scope.left);
                Assert::IsTrue(r.partialSamplingEvaluated && r.partialSamplingReaffirmed && !r.samplingEquivalent);
                Assert::IsTrue(r.currentlyPixelSafe && r.CanRetainPresentation(),
                    L"A current safe one-step outward strip must not turn contained dark-picture detail into crop withdrawal");
                Assert::IsTrue(CanRetainProvisionalSamplingCrop(scope,observed,
                    r.activePicture.classification,r.CanRetainPresentation(),r.partialSamplingReaffirmed),
                    L"Inspection and recovery must consume the same current partial-proposal proof");
                Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),
                    int(MakeActivePictureObservation(r.activePicture,40,24).classification),
                    L"Retention must not grant new crop geometry authority");
                Assert::IsFalse(CanRetainProvisionalSamplingCrop(scope,observed,
                    r.activePicture.classification,r.CanRetainPresentation()),
                    L"Partial geometry alone cannot substitute for explicit current strip proof");
            }
        }

        TEST_METHOD(PartialProposalOutwardStripConflictMustNotInheritOldBorderTolerance)
        {
            for (bool p210 : {false,true})
            for (bool above : {false,true})
            for (bool colored : {false,true})
            {
                P010Frame frame(3840,2160,64,p210);
                frame.BlackOutside(above ? 0 : 972,above ? 284 : 660,
                    above ? 2868 : 3840,above ? 1500 : 1876);
                const int row=above ? 280 : 1879;
                frame.FillRectangle(0,row,3840,row+1,colored ? 96 : 300,
                    colored ? 640 : 512,512);
                const auto scope=ScopePresentation(3840,2160,284,1876);
                const auto r=EvaluateActivePicturePresentationRetention(
                    p210 ? frame.P210Source() : frame.P010Source(),scope);
                Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(r.activePicture.classification));
                Assert::IsTrue(r.excludedBandsPixelSafe,
                    L"This counterexample deliberately hides a thin occupied strip from broad-bar statistics");
                Assert::AreEqual(above ? 280 : 660,r.activePicture.proposedBounds.top);
                Assert::AreEqual(above ? 1500 : 1880,r.activePicture.proposedBounds.bottom);
                Assert::IsTrue(r.partialSamplingEvaluated && !r.partialSamplingReaffirmed && !r.samplingEquivalent);
                Assert::IsFalse(r.currentlyPixelSafe || r.CanRetainPresentation(),
                    L"Unlike the old near-identical geometry tolerance, partial proposals require actual strip safety");
                Assert::IsFalse(CanRetainProvisionalSamplingCrop(scope,r.activePicture.proposedBounds,
                    r.activePicture.classification,r.CanRetainPresentation(),r.partialSamplingReaffirmed));
            }
        }

        TEST_METHOD(PartialProposalRejectsLargerExpansionOutwardSidesAndUntrustedBase)
        {
            for (bool above : {false,true})
            {
                P010Frame frame(3840,2160);
                frame.BlackOutside(above ? 0 : 972,above ? 284 : 660,
                    above ? 2868 : 3840,above ? 1500 : 1876);
                const int row=above ? 276 : 1883;
                frame.FillRectangle(0,row,3840,row+1,96);
                const auto scope=ScopePresentation(3840,2160,284,1876);
                const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
                Assert::IsTrue(r.excludedBandsPixelSafe);
                Assert::IsFalse(r.CanRetainPresentation(),L"More than one scan step is not a retention exception");
                Assert::IsFalse(CanRetainProvisionalSamplingCrop(scope,r.activePicture.proposedBounds,
                    r.activePicture.classification,true,true));
            }
            auto windowbox=ScopePresentation(3840,2160,284,1876);
            windowbox.left=200; windowbox.right=3640;
            windowbox.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
            auto observed=windowbox;
            observed.top=660; observed.bottom=1880; observed.left-=4;
            Assert::IsFalse(CanRetainProvisionalSamplingCrop(windowbox,observed,
                ActivePictureClassification::PROVISIONAL,true,true));
            observed.left=windowbox.left; observed.right+=4;
            Assert::IsFalse(CanRetainProvisionalSamplingCrop(windowbox,observed,
                ActivePictureClassification::PROVISIONAL,true,true));
            auto untrusted=ScopePresentation(3840,2160,284,1876);
            untrusted.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            observed=untrusted; observed.left=972; observed.top=660; observed.bottom=1880;
            Assert::IsFalse(CanRetainProvisionalSamplingCrop(untrusted,observed,
                ActivePictureClassification::PROVISIONAL,true,true));
        }

        TEST_METHOD(PartialProposalProofSurvivesNearBlackExitAndExpiredInspectionWithoutNewAcquisition)
        {
            using namespace AlphaSourceCrop;
            const auto scope=ScopePresentation(3840,2160,284,1876);
            P010Frame native(3840,2160);
            native.BlackOutside(0,284,3840,1876);
            const auto clean=ExtractActivePictureEvidence(native.P010Source());
            Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),int(clean.classification));
            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.geometry=clean.trustedBounds;
            crop.classification=clean.classification;
            crop.frameSourceGeneration=crop.geometrySourceGeneration=1;
            crop.frameSourceSequence=100;
            crop.rasterWidth=3840; crop.rasterHeight=2160;
            crop.latestObservationSupportsCrop=true;
            crop.latestObservationClassification=clean.classification;
            const auto admitted=AdmitCropPresentation({},crop,Evaluate(crop),4);
            Assert::IsTrue(admitted.presentation.applyCrop && admitted.state.available && !admitted.blocked);

            NearBlackPresentationEpisodeInput episode;
            episode.measurementCurrent=episode.nearBlackEvaluated=episode.globalNearBlack=true;
            episode.trustedCropAvailable=true; episode.trustedCrop=scope;
            episode.sourceGeneration=1; episode.presentationEpoch=4; episode.sourceSequence=101;
            episode.framesPerSecond=24;
            const auto dark=EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(dark.state.mode));

            VerticalInspectionBridgeInput inspection;
            inspection.candidate=inspection.retentionRequested=true;
            inspection.sourceGeneration=1; inspection.presentationEpoch=4;
            inspection.sourceSequence=110; inspection.trustedBase=scope;
            const auto started=UpdateVerticalInspectionBridge(inspection);
            Assert::IsTrue(started.retain && !started.state.failOpenLatched);
            inspection.previous=started.state;
            inspection.sourceSequence=140; // Well beyond the bounded inspection window.
            const auto expired=UpdateVerticalInspectionBridge(inspection);
            Assert::IsTrue(expired.expired && expired.state.failOpenLatched);

            P010Frame partial(3840,2160);
            partial.BlackOutside(972,660,3840,1876);
            partial.FillRectangle(0,1879,3840,1880,96);
            const auto retained=EvaluateActivePicturePresentationRetention(partial.P010Source(),scope);
            Assert::IsTrue(retained.excludedBandsPixelSafe && !retained.globalNearBlack);
            const uint64_t sequence=inspection.sourceSequence+1;
            episode.previous=dark.state; episode.sourceSequence=sequence;
            episode.globalNearBlack=retained.globalNearBlack; episode.sceneBoundary=true;
            const auto ended=EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE),int(ended.state.mode));

            inspection.previous=expired.state; inspection.sourceSequence=sequence;
            inspection.samplingRetentionResolved=CanRetainProvisionalSamplingCrop(scope,
                retained.activePicture.proposedBounds,retained.activePicture.classification,
                retained.CanRetainPresentation(),retained.partialSamplingReaffirmed);
            const auto resolved=UpdateVerticalInspectionBridge(inspection);
            Assert::IsFalse(resolved.state.failOpenLatched,
                L"Current safe partial-proposal proof must clear the old inspection latch after near-black scene exit");
            crop.frameSourceSequence=sequence;
            crop.latestObservationSupportsCrop=false;
            crop.latestObservationClassification=retained.activePicture.classification;
            crop.latestObservationIsProvisional=true;
            crop.frameLocalPresentationRetentionEvaluated=retained.analysisValid && retained.presentationValid;
            crop.frameLocalPresentationRetentionSafe=retained.CanRetainPresentation();
            crop.frameLocalPartialSamplingReaffirmed=retained.partialSamplingReaffirmed;
            crop.nearBlackEpisodeRetainCrop=ended.state.mode==NearBlackPresentationMode::RETAIN_CROP;
            crop.presentationFailOpen=resolved.state.failOpenLatched;
            PresentationRecoveryInput recovery;
            recovery.crop=crop; recovery.candidate=Evaluate(crop);
            recovery.presentationEpoch=4;
            recovery.measurementCurrent=recovery.retentionEvaluated=true;
            recovery.retentionBounds=scope; recovery.retentionSourceGeneration=1;
            recovery.retentionSourceSequence=sequence;
            recovery.observationAvailable=retained.activePicture.available;
            recovery.observation=retained.activePicture.proposedBounds;
            recovery.observationClassification=retained.activePicture.classification;
            recovery.excludedBandsPixelSafe=retained.excludedBandsPixelSafe;
            recovery.nearBlackEvaluated=true; recovery.globalNearBlack=retained.globalNearBlack;
            const auto recovered=EvaluatePresentationRecovery(recovery);
            Assert::IsFalse(recovered.started || recovered.state.active);
            const auto kept=AdmitCropPresentation(admitted.state,crop,recovered.presentation,4);
            Assert::IsTrue(kept.presentation.applyCrop && !kept.blocked);
            Assert::AreEqual(scope.left,kept.presentation.sourceBounds.left);
            Assert::AreEqual(scope.top,kept.presentation.sourceBounds.top);
            Assert::AreEqual(scope.right,kept.presentation.sourceBounds.right);
            Assert::AreEqual(scope.bottom,kept.presentation.sourceBounds.bottom);
            const auto startup=AdmitCropPresentation({},crop,recovered.presentation,4);
            Assert::IsTrue(startup.blocked && !startup.presentation.applyCrop,
                L"Identical retention evidence must never establish a never-presented crop");
        }
        TEST_METHOD(PartialProposalNativeV210MatchesPlanarStripProof)
        {
            // Pack identical synthetic pixels, including padded native rows.
            for (int variant=0;variant<3;++variant)
            {
                P010Frame frame(1920,1080,64,true);
                frame.BlackOutside(486,330,1920,938);
                frame.FillRectangle(0,939,1920,940,variant==1 ? 300 : 96,
                    variant==2 ? 640 : 512,512);
                const auto planar=frame.P210Source();
                const size_t pitch=1920/6*16+64;
                std::vector<uint8_t> bytes(pitch*1080,0);
                for (int y=0;y<1080;++y)
                    for (int x=0;x<1920;x+=6)
                    {
                        AnalysisLumaSample p[6];
                        for (int i=0;i<6;++i) Assert::IsTrue(planar.Sample(x+i,y,p[i]));
                        const uint32_t words[]={
                            uint32_t(p[0].chromaU) | uint32_t(p[0].luma)<<10 | uint32_t(p[0].chromaV)<<20,
                            uint32_t(p[1].luma) | uint32_t(p[2].chromaU)<<10 | uint32_t(p[2].luma)<<20,
                            uint32_t(p[2].chromaV) | uint32_t(p[3].luma)<<10 | uint32_t(p[4].chromaU)<<20,
                            uint32_t(p[4].luma) | uint32_t(p[4].chromaV)<<10 | uint32_t(p[5].luma)<<20};
                        auto target=bytes.data()+y*pitch+x/6*16;
                        for (int word=0;word<4;++word)
                            for (int b=0;b<4;++b) target[word*4+b]=static_cast<uint8_t>(words[word]>>(b*8));
                    }
                AnalysisLumaSource native{bytes.data(),bytes.size(),1920,1080,pitch,0,
                    AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,1};
                const auto scope=ScopePresentation(1920,1080,142,938);
                const auto a=EvaluateActivePicturePresentationRetention(planar,scope);
                const auto b=EvaluateActivePicturePresentationRetention(native,scope);
                Assert::IsTrue(a.partialSamplingEvaluated && b.partialSamplingEvaluated);
                Assert::AreEqual(variant==0,a.partialSamplingReaffirmed);
                Assert::AreEqual(a.partialSamplingReaffirmed,b.partialSamplingReaffirmed);
                Assert::AreEqual(a.CanRetainPresentation(),b.CanRetainPresentation());
                Assert::AreEqual(a.activePicture.proposedBounds.top,b.activePicture.proposedBounds.top);
                Assert::AreEqual(a.activePicture.proposedBounds.bottom,b.activePicture.proposedBounds.bottom);
                Assert::IsFalse(a.samplingEquivalent || b.samplingEquivalent);
            }
        }

        TEST_METHOD(PartialProposalRecoveryRequiresExplicitFreshProofForExactContract)
        {
            using namespace AlphaSourceCrop;
            P010Frame frame(3840,2160);
            frame.BlackOutside(972,660,3840,1876);
            frame.FillRectangle(0,1879,3840,1880,96);
            const auto scope=ScopePresentation(3840,2160,284,1876);
            const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
            Assert::IsTrue(r.currentlyPixelSafe && r.partialSamplingReaffirmed);
            PresentationRecoveryInput input;
            input.crop.automaticCropEnabled=input.crop.sharedGeometryAvailable=true;
            input.crop.geometry=scope;
            input.crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.crop.frameSourceGeneration=input.crop.geometrySourceGeneration=1;
            input.crop.rasterWidth=3840; input.crop.rasterHeight=2160;
            input.crop.latestObservationClassification=r.activePicture.classification;
            input.crop.latestObservationIsProvisional=true;
            input.crop.frameLocalPresentationRetentionEvaluated=true;
            input.crop.frameLocalPresentationRetentionSafe=r.CanRetainPresentation();
            input.crop.frameLocalPartialSamplingReaffirmed=r.partialSamplingReaffirmed;
            input.measurementCurrent=input.retentionEvaluated=true;
            input.retentionBounds=scope; input.retentionSourceGeneration=1;
            input.observationAvailable=r.activePicture.available;
            input.observation=r.activePicture.proposedBounds;
            input.observationClassification=r.activePicture.classification;
            input.excludedBandsPixelSafe=r.excludedBandsPixelSafe;
            input.nearBlackEvaluated=true; input.globalNearBlack=r.globalNearBlack;
            input.framesPerSecond=24; input.presentationEpoch=4;
            input.previous.active=true; input.previous.trustedCrop=scope;
            input.previous.sourceGeneration=1; input.previous.presentationEpoch=4;
            for (uint64_t sequence=100;sequence<107;++sequence)
            {
                input.crop.frameSourceSequence=input.retentionSourceSequence=sequence;
                input.candidate=Evaluate(input.crop);
                const auto result=EvaluatePresentationRecovery(input);
                Assert::AreEqual(unsigned(sequence-99),result.samples);
                Assert::AreEqual(sequence==106,result.released);
                if (sequence==106) Assert::IsTrue(result.presentation.applyCrop);
                input.previous=result.state;
            }
            input.previous.active=true; input.previous.trustedCrop=scope;
            input.previous.sourceGeneration=1; input.previous.presentationEpoch=4;
            input.previous.samples=6; input.previous.lastSourceSequence=105;
            input.crop.frameSourceSequence=input.retentionSourceSequence=106;
            auto native=input.crop;
            native.latestObservationSupportsCrop=true;
            native.latestObservationIsProvisional=false;
            native.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            const auto admitted=AdmitCropPresentation({},native,Evaluate(native),4);
            Assert::IsTrue(admitted.state.available && admitted.presentation.applyCrop && !admitted.blocked);
            for (int failure=0;failure<12;++failure)
            {
                auto bad=input;
                switch (failure)
                {
                case 0: bad.measurementCurrent=false; break;
                case 1: bad.retentionSourceGeneration=2; break;
                case 2: bad.retentionSourceSequence=105; break;
                case 3: bad.retentionBounds.top+=2; break;
                case 4: bad.crop.frameLocalPartialSamplingReaffirmed=false; break;
                case 5: bad.crop.frameLocalPresentationRetentionSafe=false; break;
                case 6: bad.excludedBandsPixelSafe=false; break;
                case 7: bad.observation.bottom+=8; break;
                case 8: bad.observation.left=-4; break;
                case 9: bad.presentationEpoch=5; break;
                case 10: bad.cadenceRepeat=true; break;
                case 11: bad.retentionEvaluated=false; break;
                }
                bad.candidate=Evaluate(bad.crop);
                const auto result=EvaluatePresentationRecovery(bad);
                const auto message=std::wstring(L"Partial proof safety case ")+std::to_wstring(failure);
                if (failure==9)
                {
                    // Recovery resets at an epoch change; final admission owns
                    // whether the candidate can be shown in the new context.
                    Assert::IsTrue(result.ended && !result.state.active && result.samples==0,message.c_str());
                    Assert::IsFalse(result.released,message.c_str());
                    const auto stale=AdmitCropPresentation(admitted.state,bad.crop,
                        result.presentation,bad.presentationEpoch);
                    Assert::IsTrue(stale.blocked && !stale.presentation.applyCrop,message.c_str());
                }
                else Assert::IsFalse(result.released || result.presentation.applyCrop,message.c_str());
            }
        }
		TEST_METHOD(AgencyTreeSamplingKeepsScopeThroughInspectionAndRecovery)
		{
			using namespace AlphaSourceCrop;
			const auto scope = ScopePresentation(3840,2160,276,1884);
			for (bool p210 : {false,true})
			for (int variant = 0; variant < 3; ++variant)
			{
				P010Frame frame(3840,2160,0,p210);
				const int left = variant < 2 ? 8 : 0;
				const int right = variant == 0 ? 3832 : variant == 1 ? 3836 : 3840;
				frame.BlackOutside(left,276,right,1884);
				frame.FillRectangle(0,1887,3840,1888,96);
				if (variant == 2) frame.FillRectangle(0,272,3840,273,96);
				const auto source = p210 ? frame.P210Source() : frame.P010Source();
				const auto r = EvaluateActivePicturePresentationRetention(source,scope);
				Assert::AreEqual(left,r.activePicture.proposedBounds.left);
				Assert::AreEqual(right,r.activePicture.proposedBounds.right);
				Assert::AreEqual(variant == 2 ? 272 : 276,r.activePicture.proposedBounds.top);
				Assert::AreEqual(1888,r.activePicture.proposedBounds.bottom);
				Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(r.activePicture.classification));
				Assert::IsTrue(r.excludedBandsPixelSafe);
				Assert::IsTrue(r.samplingReaffirmed && r.CanRetainPresentation(),
					L"Contained side measurements and one step per vertical edge must retain current safe framing");
				Input crop;
				crop.automaticCropEnabled = crop.sharedGeometryAvailable = true;
				crop.geometry = scope;
				crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.geometrySourceGeneration = crop.frameSourceGeneration = 1;
				crop.rasterWidth = 3840; crop.rasterHeight = 2160;
				crop.latestObservationClassification = r.activePicture.classification;
				crop.latestObservationIsProvisional = true;
				crop.frameLocalPresentationRetentionEvaluated = true;
				crop.frameLocalPresentationRetentionSafe = r.CanRetainPresentation();
				VerticalInspectionBridgeState previousInspection;
				previousInspection.active = previousInspection.failOpenLatched = true;
				previousInspection.sourceGeneration = 1; previousInspection.presentationEpoch = 4;
				previousInspection.trustedBase = scope; previousInspection.firstCandidateSourceSequence = 90;
				for (uint64_t seq = 100; seq < 120; ++seq)
				{
					crop.frameSourceSequence = seq;
					VerticalInspectionBridgeInput inspect;
					inspect.previous = previousInspection; inspect.candidate = true;
					inspect.denseAnalysisCompleted = true;
					inspect.sourceGeneration = 1; inspect.presentationEpoch = 4;
					inspect.sourceSequence = seq; inspect.trustedBase = scope;
					inspect.samplingRetentionResolved = CanRetainProvisionalSamplingCrop(scope,
						r.activePicture.proposedBounds,r.activePicture.classification,r.CanRetainPresentation());
					const auto bridge = UpdateVerticalInspectionBridge(inspect);
					crop.presentationFailOpen = bridge.state.failOpenLatched;
					PresentationRecoveryInput recover;
					recover.crop = crop; recover.candidate = Evaluate(crop);
					recover.presentationEpoch = 4;
					const auto result = EvaluatePresentationRecovery(recover);
					Assert::IsFalse(bridge.state.failOpenLatched || result.started || result.state.active);
					Assert::IsTrue(result.presentation.applyCrop);
					Assert::AreEqual(0,result.presentation.sourceBounds.left);
					Assert::AreEqual(3840,result.presentation.sourceBounds.right);
					Assert::AreEqual(276,result.presentation.sourceBounds.top);
					Assert::AreEqual(1884,result.presentation.sourceBounds.bottom);
					previousInspection = bridge.state;
				}
			}
		}
		TEST_METHOD(AgencyTreeSamplingStillRejectsOutwardSidesAndLargerVerticalChanges)
		{
			const auto scope = ScopePresentation(3840,2160,276,1884);
			for (int variant = 0; variant < 3; ++variant)
			{
				P010Frame frame(3840,2160);
				frame.BlackOutside(8,276,3832,1884);
				if (variant == 0) frame.FillRectangle(0,1895,3840,1896,96);
				if (variant == 1) frame.FillRectangle(0,1884,3840,2160,300);
				if (variant == 2) frame.FillRectangle(0,248,3840,276,96,640,512);
				const auto r = EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
				Assert::IsFalse(r.samplingEquivalent || r.CanRetainPresentation(),
					L"Contained side detail must not excuse larger or colored outward content");
			}
			auto windowbox = scope; windowbox.left = 200; windowbox.right = 3640;
			windowbox.trustedBarAxes = ActivePictureBounds::BarAxes::BOTH;
			auto observed = windowbox; observed.bottom += 4;
			observed.left -= 4;
			Assert::IsFalse(CanRetainProvisionalSamplingCrop(windowbox,observed,
				ActivePictureClassification::PROVISIONAL,true));
			observed.left = windowbox.left; observed.right += 4;
			Assert::IsFalse(CanRetainProvisionalSamplingCrop(windowbox,observed,
				ActivePictureClassification::PROVISIONAL,true));
		}
		TEST_METHOD(ProvisionalOneScanStepRetainsPixelSafeScope)
		{
			for (bool p210 : { false, true })
			{
				P010Frame frame(3840,2160,0,p210);
				frame.BlackOutside(0,276,3840,1884);
				// A low-contrast edge line stops the coarse scan one step early,
				// but is below the existing credible-visible-pixel threshold.
				frame.FillRectangle(0,1887,3840,1888,96);
				const auto source=p210 ? frame.P210Source() : frame.P010Source();
				const auto scope=ScopePresentation(3840,2160,276,1884);
				const auto r=EvaluateActivePicturePresentationRetention(source,scope);
				Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(r.activePicture.classification));
				Assert::AreEqual(1888,r.activePicture.proposedBounds.bottom);
				Assert::IsTrue(r.excludedBandsPixelSafe);
				Assert::IsFalse(r.proposedBoundsContained || r.outwardVisibleBoundsAvailable || r.globalNearBlack);
				Assert::IsTrue(r.samplingReaffirmed && !r.samplingStripConflict);
				Assert::IsTrue(r.currentlyPixelSafe,L"One coarse scan step must not withdraw a pixel-safe established crop");
				Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),
					int(MakeActivePictureObservation(r.activePicture,2,24).classification),
					L"Retention must not promote a provisional observation to new crop authority");
			}
		}

		TEST_METHOD(ProvisionalScanStepDoesNotArmInspectionOrRecovery)
		{
			using namespace AlphaSourceCrop;
			P010Frame frame(3840,2160);
			frame.BlackOutside(0,276,3840,1884);
			frame.FillRectangle(0,1887,3840,1888,96);
			const auto scope=ScopePresentation(3840,2160,276,1884);
			const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
			Input crop;
			crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
			crop.geometry=scope;
			crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.frameSourceGeneration=crop.geometrySourceGeneration=1;
			crop.frameSourceSequence=40081;
			crop.rasterWidth=3840; crop.rasterHeight=2160;
			crop.latestObservationClassification=r.activePicture.classification;
			crop.latestObservationIsProvisional=r.activePicture.classification==ActivePictureClassification::PROVISIONAL;
			crop.frameLocalPresentationRetentionEvaluated=r.analysisValid && r.presentationValid;
			crop.frameLocalPresentationRetentionSafe=r.CanRetainPresentation();
			auto candidate=Evaluate(crop);
			VerticalInspectionBridgeInput inspection;
			inspection.candidate=r.activePicture.proposedBounds.bottom>scope.bottom;
			inspection.retentionRequested=inspection.candidate && !candidate.applyCrop &&
				candidate.withdrawalCause==WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED;
			inspection.denseAnalysisCompleted=true;
			inspection.sourceGeneration=1; inspection.presentationEpoch=4;
			inspection.sourceSequence=crop.frameSourceSequence;
			inspection.trustedBase=scope;
			const auto bridge=UpdateVerticalInspectionBridge(inspection);
			crop.presentationFailOpen=bridge.state.failOpenLatched;
			candidate=Evaluate(crop);
			PresentationRecoveryInput recovery;
			recovery.crop=crop; recovery.candidate=candidate;
			recovery.presentationEpoch=4;
			recovery.measurementCurrent=recovery.retentionEvaluated=true;
			recovery.retentionBounds=scope;
			recovery.retentionSourceGeneration=1; recovery.retentionSourceSequence=crop.frameSourceSequence;
			recovery.observationAvailable=r.activePicture.available;
			recovery.observation=r.activePicture.proposedBounds;
			recovery.observationClassification=r.activePicture.classification;
			recovery.excludedBandsPixelSafe=r.excludedBandsPixelSafe;
			recovery.nearBlackEvaluated=true; recovery.globalNearBlack=r.globalNearBlack;
			const auto final=EvaluatePresentationRecovery(recovery);
			Assert::IsFalse(bridge.state.failOpenLatched,L"A harmless scan mismatch must not latch the inspection fallback");
			Assert::IsFalse(final.started || final.state.active,L"Avoid the false withdrawal rather than shortening its recovery");
			Assert::IsTrue(final.presentation.applyCrop);
			Assert::AreEqual(276,final.presentation.sourceBounds.top);
			Assert::AreEqual(1884,final.presentation.sourceBounds.bottom);
		}

		TEST_METHOD(BrightOneStepBorderMustNotCauseFullFrameWithdrawal)
		{
			using namespace AlphaSourceCrop;
			P010Frame frame(3840,2160);
			frame.BlackOutside(0,276,3840,1884);
			frame.FillRectangle(0,1887,3840,1888,300);
			const auto scope=ScopePresentation(3840,2160,276,1884);
			const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
			Input crop;
			crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
			crop.geometry=scope;
			crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.frameSourceGeneration=crop.geometrySourceGeneration=1;
			crop.frameSourceSequence=40081;
			crop.rasterWidth=3840; crop.rasterHeight=2160;
			crop.latestObservationClassification=r.activePicture.classification;
			crop.latestObservationIsProvisional=r.activePicture.classification==ActivePictureClassification::PROVISIONAL;
			crop.frameLocalPresentationRetentionEvaluated=r.analysisValid && r.presentationValid;
			crop.frameLocalPresentationRetentionSafe=r.CanRetainPresentation();
			auto candidate=Evaluate(crop);
			VerticalInspectionBridgeInput inspection;
			inspection.candidate=r.activePicture.proposedBounds.bottom>scope.bottom;
			inspection.retentionRequested=inspection.candidate && !candidate.applyCrop &&
				candidate.withdrawalCause==WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED;
			inspection.denseAnalysisCompleted=true;
			inspection.sourceGeneration=1; inspection.presentationEpoch=4;
			inspection.sourceSequence=crop.frameSourceSequence;
			inspection.trustedBase=scope;
			const auto bridge=UpdateVerticalInspectionBridge(inspection);
			crop.presentationFailOpen=bridge.state.failOpenLatched;
			candidate=Evaluate(crop);
			PresentationRecoveryInput recovery;
			recovery.crop=crop; recovery.candidate=candidate;
			recovery.presentationEpoch=4;
			recovery.measurementCurrent=recovery.retentionEvaluated=true;
			recovery.retentionBounds=scope;
			recovery.retentionSourceGeneration=1; recovery.retentionSourceSequence=crop.frameSourceSequence;
			recovery.observationAvailable=r.activePicture.available;
			recovery.observation=r.activePicture.proposedBounds;
			recovery.observationClassification=r.activePicture.classification;
			recovery.excludedBandsPixelSafe=r.excludedBandsPixelSafe;
			recovery.nearBlackEvaluated=true; recovery.globalNearBlack=r.globalNearBlack;
			const auto final=EvaluatePresentationRecovery(recovery);
			Assert::IsFalse(bridge.state.failOpenLatched,L"A harmless scan mismatch must not latch the inspection fallback");
			Assert::IsFalse(final.started || final.state.active,L"Avoid the false withdrawal rather than shortening its recovery");
			Assert::IsTrue(final.presentation.applyCrop);
			Assert::AreEqual(276,final.presentation.sourceBounds.top);
			Assert::AreEqual(1884,final.presentation.sourceBounds.bottom);
		}

		TEST_METHOD(ProvisionalSamplingToleranceDistinguishesEdgePixelsFromLargerExpansion)
		{
			const auto scope=ScopePresentation(3840,2160,276,1884);
			for (int variant=0;variant<4;++variant)
			{
				P010Frame frame(3840,2160);
				frame.BlackOutside(0,276,3840,1884);
				if (variant==0) frame.FillRectangle(0,1887,3840,1888,300);
				if (variant==1) frame.FillRectangle(0,1887,3840,1888,96,640,512);
				if (variant==2) frame.FillRectangle(0,1895,3840,1896,96);
				if (variant==3) frame.FillRectangle(0,1884,3840,2160,300);
				const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
				Assert::IsFalse(r.currentlyPixelSafe,L"Visible/color evidence must not be mislabeled as black pixels");
				Assert::AreEqual(variant<2,r.CanRetainPresentation(),L"Only a one-step border may use the presentation tolerance");
				if (variant<2)
				{
					Assert::IsTrue(r.excludedBandsPixelSafe,L"Whole-bar sampling alone misses this narrow strip");
					Assert::IsTrue(r.samplingStripConflict,L"Focused rows must report the real pixel conflict despite presentation tolerance");
					Assert::AreEqual(variant==0 ? 300 : 96,r.samplingStripPeakY);
					Assert::AreEqual(variant==0 ? 0 : 128,r.samplingStripPeakChromaDelta);
				}
			}
		}

		TEST_METHOD(ProvisionalScanStepIsResolutionScaledAndRequiresTrustedBars)
		{
			for (int scale : { 1, 2 })
			{
				const int width=1920*scale, height=1080*scale;
				const int top=138*scale, bottom=942*scale, step=2*scale;
				P010Frame frame(width,height);
				frame.BlackOutside(0,top,width,bottom);
				frame.FillRectangle(0,top-step,width,top-step+1,96);
                // Keep the inward contrast probe inconclusive at both scan scales.
                frame.FillRectangle(0,top,width,top+1,64);
				auto scope=ScopePresentation(width,height,top,bottom);
				const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
				Assert::AreEqual(top-step,r.activePicture.proposedBounds.top);
				Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(r.activePicture.classification));
                Assert::IsTrue(r.samplingReaffirmed && r.currentlyPixelSafe);
				scope.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
				const auto untrusted=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
				Assert::IsFalse(untrusted.samplingReaffirmed || untrusted.CanRetainPresentation());
			}
		}

		TEST_METHOD(AgencyTreeSamplingRetainsOneStepAtEachVerticalEdge)
		{
			P010Frame frame(3840,2160);
			frame.BlackOutside(0,276,3840,1884);
			frame.FillRectangle(0,272,3840,273,96);
			frame.FillRectangle(0,1887,3840,1888,96);
			const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),
				ScopePresentation(3840,2160,276,1884));
			Assert::AreEqual(272,r.activePicture.proposedBounds.top);
			Assert::AreEqual(1888,r.activePicture.proposedBounds.bottom);
			Assert::IsTrue(r.excludedBandsPixelSafe);
			Assert::IsTrue(r.samplingReaffirmed && r.CanRetainPresentation());
		}


		TEST_METHOD(ProvisionalScanStepCanCompleteExistingRecoveryWithCurrentPixelProof)
		{
			using namespace AlphaSourceCrop;
			P010Frame frame(3840,2160);
			frame.BlackOutside(0,276,3840,1884);
			frame.FillRectangle(0,1887,3840,1888,96);
			const auto scope=ScopePresentation(3840,2160,276,1884);
			const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
			Assert::IsTrue(r.samplingReaffirmed && r.currentlyPixelSafe);
			PresentationRecoveryInput input;
			input.crop.automaticCropEnabled=input.crop.sharedGeometryAvailable=true;
			input.crop.geometry=scope;
			input.crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
			input.crop.frameSourceGeneration=input.crop.geometrySourceGeneration=1;
			input.crop.rasterWidth=3840; input.crop.rasterHeight=2160;
			input.crop.latestObservationClassification=r.activePicture.classification;
			input.crop.latestObservationIsProvisional=true;
			input.crop.frameLocalPresentationRetentionEvaluated=true;
			input.crop.frameLocalPresentationRetentionSafe=r.CanRetainPresentation();
			input.measurementCurrent=input.retentionEvaluated=true;
			input.retentionBounds=scope;
			input.retentionSourceGeneration=1;
			input.observationAvailable=r.activePicture.available;
			input.observation=r.activePicture.proposedBounds;
			input.observationClassification=r.activePicture.classification;
			input.excludedBandsPixelSafe=r.excludedBandsPixelSafe;
			input.nearBlackEvaluated=true;
			input.framesPerSecond=24;
			input.presentationEpoch=4;
			input.previous.active=true;
			input.previous.trustedCrop=scope;
			input.previous.sourceGeneration=1;
			input.previous.presentationEpoch=4;
			for (uint64_t sequence=100;sequence<107;++sequence)
			{
				input.crop.frameSourceSequence=input.retentionSourceSequence=sequence;
				input.candidate=Evaluate(input.crop);
				Assert::IsTrue(input.candidate.applyCrop);
				const auto result=EvaluatePresentationRecovery(input);
				Assert::AreEqual(unsigned(sequence-99),result.samples,
					L"Current strip proof should count toward ordinary recovery without demanding new aspect authority");
				if (sequence<106) Assert::IsFalse(result.released);
				else Assert::IsTrue(result.released && result.presentation.applyCrop);
				input.previous=result.state;
			}
		}

		TEST_METHOD(ProvisionalSamplingRecoveryRejectsStaleUnsafeOrDifferentContext)
		{
			using namespace AlphaSourceCrop;
			P010Frame frame(3840,2160);
			frame.BlackOutside(0,276,3840,1884);
			frame.FillRectangle(0,1887,3840,1888,96);
			const auto scope=ScopePresentation(3840,2160,276,1884);
			const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
			Assert::IsTrue(r.samplingReaffirmed && r.currentlyPixelSafe);
			PresentationRecoveryInput input;
			input.crop.automaticCropEnabled=input.crop.sharedGeometryAvailable=true;
			input.crop.geometry=scope;
			input.crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
			input.crop.frameSourceGeneration=input.crop.geometrySourceGeneration=1;
			input.crop.rasterWidth=3840; input.crop.rasterHeight=2160;
			input.crop.latestObservationClassification=r.activePicture.classification;
			input.crop.latestObservationIsProvisional=true;
			input.crop.frameLocalPresentationRetentionEvaluated=true;
			input.crop.frameLocalPresentationRetentionSafe=r.CanRetainPresentation();
			input.measurementCurrent=input.retentionEvaluated=true;
			input.retentionBounds=scope;
			input.retentionSourceGeneration=1;
			input.observationAvailable=r.activePicture.available;
			input.observation=r.activePicture.proposedBounds;
			input.observationClassification=r.activePicture.classification;
			input.excludedBandsPixelSafe=r.excludedBandsPixelSafe;
			input.nearBlackEvaluated=true;
			input.framesPerSecond=24;
			input.presentationEpoch=4;
			input.previous.active=true;
			input.previous.trustedCrop=scope;
			input.previous.sourceGeneration=1;
			input.previous.presentationEpoch=4;

			input.crop.frameSourceSequence=input.retentionSourceSequence=100;
			input.previous.lastSourceSequence=99;
			input.previous.samples=6;
			for (int failure=0;failure<16;++failure)
			{
				auto bad=input;
				switch (failure)
				{
				case 0: bad.measurementCurrent=false; break;
				case 1: bad.retentionEvaluated=false; break;
				case 2: bad.retentionSourceGeneration=2; break;
				case 3: bad.retentionSourceSequence=99; break;
				case 4: bad.retentionBounds.top+=2; break;
				case 5: bad.retentionBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE; break;
				case 6: bad.crop.frameLocalPresentationRetentionSafe=false; break;
				case 7: bad.crop.frameLocalPresentationRetentionEvaluated=false; break;
				case 8: bad.excludedBandsPixelSafe=false; break;
				case 9: bad.globalNearBlack=true; break;
				case 10: bad.cadenceRepeat=true; break;
				case 11: bad.observation.bottom+=8; break;
				case 12: bad.observation.left-=4; break; // Outward, not harmless contained detail.
				case 13: bad.crop.presentationFailOpen=true; break;
				case 14: bad.observationClassification=ActivePictureClassification::FULL_RASTER_TRUSTED; break;
				case 15: bad.presentationEpoch=5; break;
				}
				bad.candidate=Evaluate(bad.crop);
				const auto result=EvaluatePresentationRecovery(bad);
				Assert::IsFalse(result.released,L"Invalid proof must not finish recovery");
				if (failure==15)
                    Assert::IsTrue(result.ended && !result.state.active && result.samples==0,
                        L"A new presentation epoch discards old recovery proof rather than completing it");
                else Assert::IsFalse(result.presentation.applyCrop,L"Unresolved recovery must continue exposing pixels");
			}
		}

		TEST_METHOD(ProvisionalSamplingProofCanResolveLatchedInspectionWithoutCropAuthority)
		{
			using namespace AlphaSourceCrop;
			P010Frame frame(3840,2160);
			frame.BlackOutside(0,276,3840,1884);
			frame.FillRectangle(0,1887,3840,1888,96);
			const auto scope=ScopePresentation(3840,2160,276,1884);
			const auto r=EvaluateActivePicturePresentationRetention(frame.P010Source(),scope);
			VerticalInspectionBridgeInput input;
			input.candidate=true;
			input.sourceGeneration=input.previous.sourceGeneration=1;
			input.sourceSequence=100;
			input.presentationEpoch=input.previous.presentationEpoch=4;
			input.trustedBase=input.previous.trustedBase=scope;
			input.previous.active=input.previous.failOpenLatched=true;
			input.previous.firstCandidateSourceSequence=90;
			Assert::IsTrue(UpdateVerticalInspectionBridge(input).state.failOpenLatched,
				L"A candidate without current proof must not clear the existing latch");
			input.samplingRetentionResolved=CanRetainProvisionalSamplingCrop(
				scope,r.activePicture.proposedBounds,r.activePicture.classification,r.CanRetainPresentation());
			Assert::IsTrue(input.samplingRetentionResolved);
			const auto result=UpdateVerticalInspectionBridge(input);
			Assert::IsFalse(result.state.active || result.state.failOpenLatched);
			Assert::IsFalse(input.cropAuthorityResolved);
			Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(r.activePicture.classification));
		}

		TEST_METHOD(CleanScopeBarsAreSafeForTrustedPresentationRetention)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsTrue(retention.analysisValid);
			Assert::IsTrue(retention.presentationValid);
			Assert::IsTrue(retention.proposedBoundsAvailable);
			Assert::IsTrue(retention.proposedBoundsContained);
			Assert::IsTrue(retention.excludedBandsPixelSafe);
			Assert::IsTrue(retention.currentlyPixelSafe);
			Assert::IsFalse(retention.globalNearBlack);
		}

		TEST_METHOD(NearBlackOutwardEntryRecoversOnlyPixelSafeEstablishedPicture)
		{
			using namespace AlphaSourceCrop;
			for (int content = 0; content < 5; ++content)
			{
				const auto scope = ScopePresentation(960, 540, 70, 470);
				P010Frame frame(960, 540);
				frame.Fill(136, 512, 512);
				frame.BlackOutside(0, 70, 960, 470, 64);
				ActivePictureTransitionModel model;
				ActivePictureTransitionDecision transition;
				const auto clean = ExtractActivePictureEvidence(frame.P010Source());
				for (uint64_t seq = 1; seq <= 20; ++seq)
					transition = model.Observe(MakeActivePictureObservation(clean, seq, 24.0));
				Assert::IsTrue(transition.stable);
				Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),
					int(transition.authoritativeClassification));
				Assert::AreEqual(70, transition.stableBounds.top);
				Assert::AreEqual(470, transition.stableBounds.bottom);

				// Darkness and an outside-band title arrive on the same frame.
				frame.Fill(64, 512, 512);
				frame.FillRectangle(300, 482, 660, 500, 200);
				const auto entry = EvaluateActivePicturePresentationRetention(frame.P010Source(), scope);
				Assert::IsTrue(entry.globalNearBlack && entry.outwardVisibleBoundsAvailable);
				NearBlackPresentationEpisodeInput input;
				input.measurementCurrent = input.nearBlackEvaluated = true;
				input.trustedCropAvailable = true;
				input.trustedCrop = scope;
				input.globalNearBlack = entry.globalNearBlack;
				input.boundedVisibleContentOutsideCrop = entry.outwardVisibleBoundsAvailable;
				input.sourceGeneration = input.presentationEpoch = 1;
				input.sourceSequence = 100;
				input.framesPerSecond = 24;
				input.previous = EvaluateNearBlackPresentationEpisode(input).state;
				Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER), int(input.previous.mode));

				// Asymmetric illumination inside the scope frame must stay provisional.
				frame.Fill(64, 512, 512);
				frame.FillRectangle(0, 104, 668, 470, 136);
				if (content == 1) frame.FillRectangle(300, 482, 660, 500, 200);
				if (content == 2) frame.FillRectangle(300, 482, 660, 500, 64, 700, 512);
				if (content == 3) frame.Fill(136, 512, 512);
				if (content == 4) frame.Fill(64, 512, 512);
				const auto retained = EvaluateActivePicturePresentationRetention(frame.P010Source(), scope);
				if (content == 0)
				{
					Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),
						int(retained.activePicture.classification));
					Assert::IsTrue(retained.CanRetainPresentation());
					Assert::IsFalse(retained.globalNearBlack);
				}
				for (uint64_t seq = 101; seq <= 107; ++seq)
				{
					const auto observation = ConstrainNearBlackCropAcquisition(retained.activePicture, true);
					transition = model.Observe(MakeActivePictureObservation(observation, seq, 24.0));
					input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = seq;
					input.globalNearBlack = retained.globalNearBlack;
					input.boundedVisibleContentOutsideCrop = retained.outwardVisibleBoundsAvailable;
					input.currentObservationAvailable = retained.activePicture.available;
					input.currentObservation = retained.activePicture.proposedBounds;
					input.currentObservationClassification = retained.activePicture.classification;
					input.retentionEvaluated = retained.analysisValid && retained.presentationValid;
					input.retentionSafe = retained.CanRetainPresentation();
					input.retentionExcludedBandsPixelSafe = retained.excludedBandsPixelSafe;
					input.retentionBounds = scope;
					input.retentionSourceGeneration = 1;
					input.knownTrustedGeometryReacquired = transition.stable;
					input.reacquisitionIsCurrentAssociation = true;
					input.reacquiredTrustedGeometry = transition.stableBounds;
					input.reacquiredTrustedClassification = transition.authoritativeClassification;
					input.reacquiredSourceGeneration = input.reacquiredPresentationEpoch = 1;
					const auto decision = EvaluateNearBlackPresentationEpisode(input);
					Assert::AreEqual(content == 0 && seq == 107, decision.releasedToTrustedCrop);
					Input crop;
					crop.automaticCropEnabled = crop.sharedGeometryAvailable = true;
					crop.geometry = scope;
					crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
					crop.frameSourceGeneration = crop.geometrySourceGeneration = 1;
					crop.rasterWidth = 960; crop.rasterHeight = 540;
					crop.latestObservationIsProvisional = true;
					crop.frameLocalPresentationRetentionEvaluated = input.retentionEvaluated;
					crop.frameLocalPresentationRetentionSafe = input.retentionSafe;
					crop.nearBlackEpisodeFullRaster = decision.state.mode == NearBlackPresentationMode::FULL_RASTER;
					const auto presentation = Evaluate(crop);
					Assert::AreEqual(content == 0 && seq == 107, presentation.applyCrop);
					if (presentation.applyCrop)
					{
						Assert::AreEqual(0, presentation.sourceBounds.left);
						Assert::AreEqual(70, presentation.sourceBounds.top);
						Assert::AreEqual(960, presentation.sourceBounds.right);
						Assert::AreEqual(470, presentation.sourceBounds.bottom);
					}
					input.previous = decision.state;
				}
			}
		}

		TEST_METHOD(ContainedDarkProvisionalFrameRetainsWithoutInnerContrast)
		{
			P010Frame frame(320, 180);
			frame.Fill(120, 512, 512);
			// Six-line limited-range bars have safe pixels but only 16 codes of
			// inner contrast, below the acquisition requirement for small bars.
			frame.BlackOutside(0, 6, 320, 174, 104, 512, 512);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 6, 174));

			Assert::AreEqual(
				static_cast<int>(ActivePictureClassification::PROVISIONAL),
				static_cast<int>(retention.activePicture.classification));
			Assert::IsFalse(retention.globalNearBlack);
			Assert::IsTrue(retention.proposedBoundsContained);
			Assert::IsFalse(retention.excludedTop.trusted);
			Assert::IsTrue(retention.excludedBandsPixelSafe);
			Assert::IsTrue(retention.currentlyPixelSafe);
		}

		TEST_METHOD(DiagnosticGridPreservesSubtleBlackAndChromaDifferences)
		{
			P010Frame frame(320, 180);
			frame.Fill(72, 514, 508);
			frame.BlackOutside(0, 22, 320, 158, 64, 512, 512);
			frame.FillRectangle(120, 60, 200, 100, 900);
			const auto grid = SampleActivePictureDiagnosticGrid(frame.P010Source());
			Assert::AreEqual(128, grid.columns);
			Assert::AreEqual(180, grid.rows);
			Assert::AreEqual(size_t(128 * 180), grid.samples.size());
			for (int row = 0; row < grid.rows; ++row)
				for (int column = 0; column < grid.columns; ++column)
				{
					const int x = column * 319 / 127;
					const bool bar = row < 22 || row >= 158;
					const bool text = x >= 120 && x < 200 && row >= 60 && row < 100;
					const auto& pixel = grid.samples[row * grid.columns + column];
					Assert::AreEqual(bar ? 64 : text ? 900 : 72, int(pixel.luma));
					Assert::AreEqual(bar || text ? 512 : 514, int(pixel.chromaU));
					Assert::AreEqual(bar || text ? 512 : 508, int(pixel.chromaV));
				}
		}

		TEST_METHOD(DiagnosticGridIsBoundedAndRejectsInvalidSource)
		{
			P010Frame frame(3840, 2160);
			const auto grid = SampleActivePictureDiagnosticGrid(frame.P010Source());
			Assert::AreEqual(128, grid.columns);
			Assert::AreEqual(270, grid.rows);
			Assert::AreEqual(size_t(34560), grid.samples.size());
			auto invalid = frame.P010Source();
			invalid.dataBytes = 1;
			Assert::IsTrue(SampleActivePictureDiagnosticGrid(invalid).samples.empty());
		}

		TEST_METHOD(BrightLogoWithoutAcquisitionGeometryKeepsEstablishedCrop)
		{
			const auto scope = ScopePresentation(3840, 2160, 208, 1952);
			P010Frame frame(3840, 2160);
			frame.Fill(64, 512, 512);
			// About 14% bright pixels: this must work above the global P90
			// darkness cutoff, even when acquisition cannot bound the logo.
			frame.FillRectangle(720, 840, 3120, 1320, 724);
			const auto evidence = EvaluateP010ActivePicturePresentationRetention(frame.View(), scope);
			Assert::IsFalse(evidence.globalNearBlack);
			Assert::IsFalse(evidence.activePicture.available);
			Assert::IsFalse(evidence.proposedBoundsAvailable);
			Assert::IsTrue(evidence.excludedBandsPixelSafe);
			Assert::IsFalse(evidence.outwardVisibleBoundsAvailable);
			Assert::IsTrue(evidence.currentlyPixelSafe);

			AlphaSourceCrop::Input input;
			input.automaticCropEnabled = input.sharedGeometryAvailable = true;
			input.geometry = scope;
			input.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.geometrySourceGeneration = input.frameSourceGeneration = 3;
			input.rasterWidth = 3840; input.rasterHeight = 2160;
			input.latestObservationIsUnavailable = true;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = evidence.currentlyPixelSafe;
			// Pause/repeat duration and expired scene holds cannot turn missing
			// boundaries into a new format while fresh margins remain safe.
			for (uint64_t sequence : {1ULL, 60ULL, 240ULL, 3600ULL})
			{
				input.frameSourceSequence = sequence;
				const auto decision = AlphaSourceCrop::Evaluate(input);
				Assert::IsTrue(decision.applyCrop);
				Assert::AreEqual(scope.top, decision.sourceBounds.top);
				Assert::AreEqual(scope.bottom, decision.sourceBounds.bottom);
			}
			input.sharedGeometryAvailable = false;
			Assert::IsFalse(AlphaSourceCrop::Evaluate(input).applyCrop);
			input.sharedGeometryAvailable = true;
			input.frameSourceGeneration = 4;
			Assert::IsFalse(AlphaSourceCrop::Evaluate(input).applyCrop);
		}

		TEST_METHOD(UnavailableLogoGeometryCannotHideVisibleMarginContent)
		{
			const auto scope = ScopePresentation(3840, 2160, 208, 1952);
			P010Frame frame(3840, 2160);
			frame.Fill(64, 512, 512);
			frame.FillRectangle(720, 840, 3120, 1320, 724);
			frame.FillRectangle(1740, 100, 1900, 140, 900);
			const auto evidence = EvaluateP010ActivePicturePresentationRetention(frame.View(), scope);
			Assert::IsFalse(evidence.excludedBandsPixelSafe);
			Assert::IsFalse(evidence.currentlyPixelSafe);
			Assert::IsTrue(evidence.outwardVisibleBoundsAvailable);
			Assert::IsTrue(evidence.outwardVisibleBounds.top <= 100);
		}

		TEST_METHOD(LogoRetentionStillRejectsRealExpansionAndInvalidPixels)
		{
			const auto scope = ScopePresentation(3840, 2160, 208, 1952);
			P010Frame frame(3840, 2160);
			// A real expansion from roughly 2.20 to 1.90 exposes picture in
			// both old margins and must withdraw the saved crop immediately.
			frame.BlackOutside(0, 70, 3840, 2090);
			const auto expanded = EvaluateP010ActivePicturePresentationRetention(frame.View(), scope);
			Assert::IsFalse(expanded.currentlyPixelSafe);
			Assert::IsFalse(expanded.excludedBandsPixelSafe);
			Assert::IsTrue(expanded.outwardVisibleBoundsAvailable);
			const auto invalid = EvaluateP010ActivePicturePresentationRetention(frame.View(1), scope);
			Assert::IsFalse(invalid.analysisValid);
			Assert::IsFalse(invalid.currentlyPixelSafe);
		}

		TEST_METHOD(ColoredOrVisibleExcludedBandsRejectRetention)
		{
			const ActivePictureBounds presentation =
				ScopePresentation(320, 180, 22, 158);
			P010Frame colored(320, 180);
			colored.BlackOutside(0, 22, 320, 158, 64, 400, 620);
			auto retention =
				EvaluateP010ActivePicturePresentationRetention(colored.View(),
					presentation);
			Assert::IsTrue(retention.proposedBoundsContained);
			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);

			P010Frame visible(320, 180);
			visible.BlackOutside(0, 22, 320, 158);
			visible.FillRectangle(0, 8, 320, 18, 600);
			retention = EvaluateP010ActivePicturePresentationRetention(
				visible.View(), presentation);
			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);
		}

		ActivePictureBounds PillarPresentation(int width, int height,
			int left, int right)
		{
			return { left, 0, right, height, width, height,
				static_cast<double>(right - left) / height,
				ActivePictureBounds::BarAxes::LEFT_RIGHT };
		}

		TEST_METHOD(NarrowTopOverlayProducesMinimalOutwardVisibleEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			// Narrow enough that the whole-row bar detector still sees a black
			// row, like sparse text/icons in a source-baked volume control.
			frame.FillRectangle(120, 8, 136, 18, 600);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);
			Assert::AreEqual(
				static_cast<int>(ActivePictureClassification::PROVISIONAL),
				static_cast<int>(retention.activePicture.classification));
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.top <= 8);
			Assert::AreEqual(158, retention.outwardVisibleBounds.bottom);
			Assert::AreEqual(0, retention.outwardVisibleBounds.left);
			Assert::AreEqual(320, retention.outwardVisibleBounds.right);
		}

		TEST_METHOD(PersistentFullWidthTopOverlayCannotBecomeProgramAspect)
		{
			const ActivePictureBounds presentation =
				ScopePresentation(320, 180, 22, 158);
			P010Frame stable(320, 180);
			stable.BlackOutside(0, 22, 320, 158);
			const auto stableEvidence =
				ExtractP010ActivePictureEvidence(stable.View());
			Assert::AreEqual(
				static_cast<int>(ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(stableEvidence.classification));

			ActivePictureTransitionModel model;
			uint64_t frameNumber = 1;
			for (uint8_t count = 0;
				count < ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++count, ++frameNumber)
			{
				model.Observe({ stableEvidence.trustedBounds, frameNumber, true,
					ActivePictureClassification::BAR_CROP_TRUSTED });
			}

			P010Frame overlay(320, 180);
			overlay.BlackOutside(0, 22, 320, 158);
			overlay.FillRectangle(0, 8, 320, 18, 600);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(
					overlay.View(), presentation);
			Assert::AreEqual(
				static_cast<int>(ActivePictureClassification::PROVISIONAL),
				static_cast<int>(retention.activePicture.classification));
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);

			for (int count = 0; count < 60; ++count, ++frameNumber)
			{
				const auto decision = model.Observe({
					retention.activePicture.proposedBounds, frameNumber, true,
					retention.activePicture.classification });
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
				Assert::AreEqual(stableEvidence.trustedBounds.top,
					decision.stableBounds.top);
				Assert::AreEqual(stableEvidence.trustedBounds.bottom,
					decision.stableBounds.bottom);
			}
		}

		TEST_METHOD(NativeP210TopOverlayUsesTheSameOutwardFitPolicy)
		{
			P010Frame frame(320, 180, 0, true);
			frame.BlackOutside(0, 22, 320, 158);
			// Exercise P210 chroma row addressing as well as luma: neutral Y=96
			// is below the luma gate, so color supplies the visible evidence.
			frame.FillRectangle(120, 8, 136, 18, 96, 300, 700);
			const auto retention = EvaluateActivePicturePresentationRetention(
				frame.P210Source(), ScopePresentation(320, 180, 22, 158));

			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.top <= 8);
			Assert::AreEqual(158, retention.outwardVisibleBounds.bottom);
		}

		TEST_METHOD(NativeP210ScopeUsesTheSameAxisAuthorityContract)
		{
			P010Frame frame(320, 180, 0, true);
			frame.BlackOutside(0, 22, 320, 158);
			const auto evidence =
				ExtractActivePictureEvidence(frame.P210Source());
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::TOP_BOTTOM),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(FourKSmallTranslucentTopControlIsStillBounded)
		{
			P010Frame frame(3840, 2160);
			frame.BlackOutside(0, 280, 3840, 1880);
			// A 32x10 neutral control at a hostile lattice phase. This is much
			// smaller and darker than the broad Apple TV volume overlay.
			frame.FillRectangle(1, 80, 33, 90, 100);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(3840, 2160, 280, 1880));

			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.top <= 80);
			Assert::IsTrue(retention.outwardVisibleBounds.top > 0);
			Assert::AreEqual(1880, retention.outwardVisibleBounds.bottom);
			Assert::IsTrue(retention.lumaSamples < 50000);
		}

		TEST_METHOD(FourKNearThresholdTwoRowFringeMatchesReplayWitness)
		{
			for (bool p210 : { false, true })
			{
				P010Frame frame(3840, 2160, 0, p210);
				frame.BlackOutside(0, 42, 3840, 2118);
				// Exact 256-column lattice positions from the replay: 3 supported
				// samples at y=40 and 11 at y=41, just over the luma cutoff.
				for (int sample = 106; sample < 117; ++sample)
				{
					const int x = ((2 * sample + 1) * 3840) / (2 * 256);
					if (sample < 109)
						frame.FillRectangle(x, 40, x + 1, 41, 99);
					frame.FillRectangle(x, 41, x + 1, 42, sample == 116 ? 103 : 99);
				}
				const auto source = p210 ? frame.P210Source() : frame.P010Source();
				const auto retention = EvaluateActivePicturePresentationRetention(source,
					ScopePresentation(3840, 2160, 42, 2118));
				Assert::IsTrue(retention.analysisValid && retention.presentationValid);
				Assert::IsFalse(retention.globalNearBlack);
				Assert::IsTrue(retention.visibleTop.available);
				Assert::AreEqual(40, retention.visibleTop.firstLine);
				Assert::AreEqual(41, retention.visibleTop.secondLine);
				Assert::AreEqual(1597, retention.visibleTop.firstX);
				Assert::AreEqual(99, retention.visibleTop.firstLuma);
				Assert::AreEqual(96, retention.visibleTop.lumaCutoff);
				Assert::AreEqual(3, retention.visibleTop.firstLineSupport);
				Assert::AreEqual(11, retention.visibleTop.secondLineSupport);
				Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
				Assert::AreEqual(27, retention.outwardVisibleBounds.top);
				Assert::AreEqual(2118, retention.outwardVisibleBounds.bottom);
				Assert::IsTrue(retention.IsWeakBoundedFringe(
					ScopePresentation(3840, 2160, 42, 2118)));
			}
		}

		TEST_METHOD(P010ReplayFringeRetainsEstablishedCropAcrossManyFrames)
		{
			using namespace AlphaSourceCrop;
			const auto crop = ScopePresentation(3840, 2160, 42, 2118);
			P010Frame frame(3840, 2160);
			frame.BlackOutside(0, 42, 3840, 2118);
			for (int sample = 106; sample < 117; ++sample)
			{
				const int x = ((2 * sample + 1) * 3840) / (2 * 256);
				if (sample < 109)
					frame.FillRectangle(x, 40, x + 1, 41, 99);
				frame.FillRectangle(x, 41, x + 1, 42,
					sample == 116 ? 103 : 99);
			}
			const auto retention = EvaluateP010ActivePicturePresentationRetention(
				frame.View(), crop);
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.IsWeakBoundedFringe(crop));

			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = input.nearBlackEvaluated = true;
			input.globalNearBlack = input.trustedCropAvailable = true;
			input.trustedCrop = crop;
			input.sourceGeneration = input.presentationEpoch = 1;
			input.sourceSequence = 100;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
				int(decision.state.mode));

			input.globalNearBlack = false;
			input.boundedVisibleContentOutsideCrop =
				retention.outwardVisibleBoundsAvailable;
			input.weakBoundedFringe = retention.IsWeakBoundedFringe(crop);
			input.minorVerticalOutwardExtent =
				retention.IsMinorVerticalOutwardExtent(crop);
			Assert::IsTrue(input.minorVerticalOutwardExtent);
			for (uint64_t sequence = 101; sequence <= 148; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::IsFalse(decision.changedToFullRaster);
				Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
					int(decision.state.mode));
				Assert::AreEqual(42, decision.state.entryTrustedCrop.top);
				Assert::AreEqual(2118, decision.state.entryTrustedCrop.bottom);
			}
		}

		TEST_METHOD(FourKBrightMenuPixelsWithinToleranceKeepEstablishedCrop)
		{
			using namespace AlphaSourceCrop;
			const auto crop = ScopePresentation(3840, 2160, 42, 2118);
			for (bool p210 : { false, true })
			{
				P010Frame frame(3840, 2160, 0, p210);
				frame.BlackOutside(0, 42, 3840, 2118);
				// Bright, real menu content spans the entire shallow top bar.
				frame.FillRectangle(1500, 0, 1800, 42, 600);
				const auto retention = EvaluateActivePicturePresentationRetention(
					p210 ? frame.P210Source() : frame.P010Source(), crop);
				Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
				Assert::AreEqual(0, retention.outwardVisibleBounds.top);
				Assert::IsFalse(retention.IsWeakBoundedFringe(crop));
				Assert::IsTrue(retention.IsMinorVerticalOutwardExtent(crop));

				NearBlackPresentationEpisodeInput input;
				input.measurementCurrent = input.nearBlackEvaluated = true;
				input.globalNearBlack = input.trustedCropAvailable = true;
				input.trustedCrop = crop;
				input.sourceGeneration = input.presentationEpoch = 1;
				input.sourceSequence = 100;
				auto decision = EvaluateNearBlackPresentationEpisode(input);
				input.previous = decision.state;
				input.globalNearBlack = false;
				input.boundedVisibleContentOutsideCrop = true;
				input.minorVerticalOutwardExtent = true;
				for (uint64_t sequence = 101; sequence <= 124; ++sequence)
				{
					input.previous = decision.state;
					input.sourceSequence = sequence;
					decision = EvaluateNearBlackPresentationEpisode(input);
					Assert::IsFalse(decision.changedToFullRaster);
					Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
						int(decision.state.mode));
				}
			}
		}

		TEST_METHOD(FourKScopeTitleBeyondToleranceCanExpand)
		{
			const auto crop = ScopePresentation(3840, 2160, 280, 1880);
			P010Frame frame(3840, 2160);
			frame.BlackOutside(0, 280, 3840, 1880);
			frame.FillRectangle(1500, 0, 1800, 42, 600);
			const auto retention = EvaluateP010ActivePicturePresentationRetention(
				frame.View(), crop);
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsFalse(retention.IsMinorVerticalOutwardExtent(crop));
		}

		TEST_METHOD(DisconnectedAdjacentRowSpecklesCurrentlyReportOutwardExtent)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			// Each adjacent depth row has enough hits, but the two tiny groups
			// have no spatial overlap or plausible picture continuity.
			frame.FillRectangle(60, 8, 64, 9, 600);
			frame.FillRectangle(260, 9, 264, 10, 600);
			const auto retention = EvaluateActivePicturePresentationRetention(
				frame.P010Source(), ScopePresentation(320, 180, 22, 158));
			Assert::IsTrue(retention.visibleTop.available);
			Assert::IsTrue(retention.visibleTop.firstLineSupport >= 2);
			Assert::IsTrue(retention.visibleTop.secondLineSupport >= 2);
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsFalse(retention.IsWeakBoundedFringe(
				ScopePresentation(320, 180, 22, 158)));
		}

		TEST_METHOD(ConnectedNarrowGlyphReportsOutwardExtent)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(60, 8, 64, 10, 600);
			const auto retention = EvaluateActivePicturePresentationRetention(
				frame.P010Source(), ScopePresentation(320, 180, 22, 158));
			Assert::IsTrue(retention.visibleTop.available);
			Assert::IsTrue(retention.visibleTop.firstLineSupport >= 2);
			Assert::IsTrue(retention.visibleTop.secondLineSupport >= 2);
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsFalse(retention.IsWeakBoundedFringe(
				ScopePresentation(320, 180, 22, 158)));
		}

		TEST_METHOD(FourKRealTopTitleHasSubstantialOutwardPixelExtent)
		{
			for (bool p210 : { false, true })
			{
				P010Frame frame(3840, 2160, 0, p210);
				frame.BlackOutside(0, 42, 3840, 2118);
				frame.FillRectangle(1500, 12, 1800, 32, 300);
				const auto source = p210 ? frame.P210Source() : frame.P010Source();
				const auto retention = EvaluateActivePicturePresentationRetention(source,
					ScopePresentation(3840, 2160, 42, 2118));
				Assert::IsTrue(retention.visibleTop.available);
				Assert::IsTrue(retention.visibleTop.firstLine <= 13);
				Assert::IsTrue(retention.visibleTop.firstLineSupport >= 10);
				Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
				Assert::IsTrue(retention.outwardVisibleBounds.top <= 12);
				Assert::IsFalse(retention.IsWeakBoundedFringe(
					ScopePresentation(3840, 2160, 42, 2118)));
			}
		}

		TEST_METHOD(BottomAndSideOverlaysExpandOnlyTheirOccupiedEdges)
		{
			P010Frame bottom(320, 180);
			bottom.BlackOutside(0, 22, 320, 158);
			bottom.FillRectangle(184, 8, 200, 18, 600);
			bottom.FillRectangle(120, 162, 136, 172, 600);
			auto retention = EvaluateP010ActivePicturePresentationRetention(
				bottom.View(), ScopePresentation(320, 180, 22, 158));
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.top <= 8);
			Assert::IsTrue(retention.outwardVisibleBounds.bottom >= 172);

			P010Frame side(320, 180);
			side.BlackOutside(40, 0, 280, 180);
			side.FillRectangle(8, 70, 18, 86, 600);
			side.FillRectangle(302, 94, 312, 110, 600);
			retention = EvaluateP010ActivePicturePresentationRetention(
				side.View(), PillarPresentation(320, 180, 40, 280));
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.left <= 8);
			Assert::IsTrue(retention.outwardVisibleBounds.right >= 312);
		}

		TEST_METHOD(IsolatedTopBarNoiseCannotAuthorizeAnOutwardEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(120, 8, 136, 9, 600);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsFalse(retention.outwardVisibleBoundsAvailable);
		}

		TEST_METHOD(AllBlackFrameIsValidNearBlackWithoutGeometry)
		{
			P010Frame frame(320, 180);
			frame.Fill(64, 512, 512);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsTrue(retention.analysisValid);
			Assert::IsTrue(retention.globalNearBlack);
			Assert::IsTrue(retention.globalLumaP90 <= 64.0);
			Assert::IsFalse(retention.activePicture.available);
			Assert::IsFalse(retention.proposedBoundsAvailable);
			Assert::IsTrue(retention.excludedBandsPixelSafe);
			Assert::IsTrue(retention.currentlyPixelSafe);
		}

		TEST_METHOD(InvalidSourceCannotProveNearBlackOrPixelSafety)
		{
			P010Frame frame(320, 180, 16);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(1),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsFalse(retention.analysisValid);
			Assert::IsFalse(retention.globalNearBlack);
			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);
		}

		TEST_METHOD(NearBlackFrameCannotReplaceRetainedPresentationGeometry)
		{
			ActivePicturePresentationRetentionEvidence retention;
			retention.analysisValid = true;
			retention.presentationValid = true;
			retention.globalNearBlack = true;
			retention.activePicture.available = true;
			retention.activePicture.classification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			retention.activePicture.trustedBounds =
				ScopePresentation(320, 180, 8, 172);

			const ActivePictureBounds retained =
				ScopePresentation(320, 180, 22, 158);
			const auto constrained = ConstrainNearBlackGeometryChange(
				retention, retained);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::PROVISIONAL),
				static_cast<int>(constrained.classification));
			Assert::AreEqual(8, constrained.proposedBounds.top);
			Assert::AreEqual(172, constrained.proposedBounds.bottom);

			retention.activePicture.trustedBounds = retained;
			const auto unchanged = ConstrainNearBlackGeometryChange(
				retention, retained);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(unchanged.classification));
		}

		TEST_METHOD(GlobalNearBlackIsEvaluatedWithoutTrustedPresentation)
		{
			P010Frame frame(320, 180);
			frame.Fill(64, 512, 512);
			// Sparse bright title strokes occupy far less than the global P90 grid.
			frame.FillRectangle(112, 68, 208, 76, 700);
			frame.FillRectangle(96, 92, 224, 100, 700);

			const auto global = EvaluateP010ActivePictureGlobalNearBlack(
				frame.View());
			Assert::IsTrue(global.evaluated);
			Assert::IsTrue(global.nearBlack);
			Assert::IsTrue(global.lumaP90 <= 96.0);
			Assert::AreEqual(static_cast<size_t>(256), global.lumaSamples);
		}

		TEST_METHOD(NearBlackEpisodeBlocksOnlyNewBarCropAuthority)
		{
			ActivePictureEvidence bars;
			bars.available = true;
			bars.classification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			bars.trustedBounds = ScopePresentation(320, 180, 22, 158);

			const auto blocked = ConstrainNearBlackCropAcquisition(bars, true);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::PROVISIONAL),
				static_cast<int>(blocked.classification));
			Assert::AreEqual(22, blocked.proposedBounds.top);
			Assert::AreEqual(158, blocked.proposedBounds.bottom);

			bars.classification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			bars.trustedBounds = ScopePresentation(320, 180, 0, 180);
			const auto full = ConstrainNearBlackCropAcquisition(bars, true);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::FULL_RASTER_TRUSTED),
				static_cast<int>(full.classification));
		}
        TEST_METHOD(ColorCorroborationRejectsUniformNeutralAndTintedDarkFrames)
        {
            P010Frame frame(384, 216);
            auto source = frame.P010Source();
            source.encoding = VideoFrameEncoding::V210;
            for (int tint : { 512, 514, 530 })
            {
                frame.Fill(87, 510, tint);
                const auto evidence = EvaluateFullRasterColorEvidence(source);
                Assert::IsTrue(evidence.evaluated);
                Assert::IsFalse(evidence.candidateSupported);
                Assert::AreEqual(static_cast<size_t>(4608), evidence.sampleCount);
            }
        }

        TEST_METHOD(ColorCorroborationRejectsUnknownAndEightBitPrecision)
        {
            P010Frame frame(384, 216);
            for (auto encoding : { VideoFrameEncoding::UNKNOWN, VideoFrameEncoding::UYVY,
                VideoFrameEncoding::HDYC, VideoFrameEncoding::BGRA_8BIT, VideoFrameEncoding::ARGB_8BIT })
            {
                auto source = frame.P010Source();
                source.encoding = encoding;
                const auto evidence = EvaluateFullRasterColorEvidence(source);
                Assert::IsFalse(evidence.evaluated);
                Assert::IsFalse(evidence.precisionSupported);
                Assert::IsFalse(evidence.candidateSupported);
                Assert::AreEqual(static_cast<size_t>(0), evidence.sampleCount);
            }
        }

        TEST_METHOD(ColorCorroborationRejectsInvalidSourceAndCentralLogo)
        {
            Assert::IsFalse(EvaluateFullRasterColorEvidence({}).evaluated);
            P010Frame frame(384, 216);
            frame.Fill(87, 510, 514);
            frame.FillRectangle(160, 80, 224, 136, 800);
            auto source = frame.P010Source();
            source.encoding = VideoFrameEncoding::V210;
            const auto evidence = EvaluateFullRasterColorEvidence(source);
            Assert::IsTrue(evidence.evaluated);
            Assert::IsFalse(evidence.candidateSupported);
        }

        TEST_METHOD(ColorCorroborationP010AndP210AgreeOnDistributedDetail)
        {
            P010Frame p010(384, 216, 16);
            P010Frame p210(384, 216, 24, true);
            p010.Fill(87, 510, 514);
            p210.Fill(87, 510, 514);
            for (int y = 0; y < 216; ++y)
                for (int x = 0; x < 384; ++x)
                {
                    const int code = 84 + ((x * 13 + y * 7) % 9);
                    WriteCode(p010.bytes.data() + y * p010.pitch + x * 2, code);
                    WriteCode(p210.bytes.data() + y * p210.pitch + x * 2, code);
                }
            auto source = p010.P010Source();
            source.encoding = VideoFrameEncoding::V210;
            const auto a = EvaluateFullRasterColorEvidence(source);
            const auto b = EvaluateFullRasterColorEvidence(p210.P210Source());
            Assert::IsTrue(a.evaluated && b.evaluated);
            Assert::AreEqual(a.candidateSupported, b.candidateSupported);
            Assert::AreEqual(a.sampleCount, b.sampleCount);
            for (int edge = 0; edge < 4; ++edge)
            {
                Assert::AreEqual(a.edges[edge].medianY, b.edges[edge].medianY);
                Assert::AreEqual(a.edges[edge].medianU, b.edges[edge].medianU);
                Assert::AreEqual(a.edges[edge].medianV, b.edges[edge].medianV);
                Assert::AreEqual(a.edges[edge].dispersionY, b.edges[edge].dispersionY);
                Assert::AreEqual(a.edges[edge].supportedCells, b.edges[edge].supportedCells);
            }
        }

        TEST_METHOD(ColorCorroborationRejectsNoisyTintedBarsWithInteriorBoundary)
        {
            P010Frame frame(384, 216);
            frame.Fill(104, 510, 522);
            frame.BlackOutside(0, 20, 384, 196, 87, 510, 514);
            for (int y = 0; y < 216; ++y)
                for (int x = 0; x < 384; ++x)
                    if (y < 20 || y >= 196)
                        WriteCode(frame.bytes.data() + y * frame.pitch + x * 2,
                            84 + ((x * 13 + y * 7) % 9));
            auto source = frame.P010Source();
            source.encoding = VideoFrameEncoding::V210;
            const auto evidence = EvaluateFullRasterColorEvidence(source);
            Assert::IsTrue(evidence.evaluated);
            Assert::IsFalse(evidence.candidateSupported);
            Assert::IsTrue(evidence.edges[1].maxBackgroundDeltaY > 8.0 ||
                evidence.edges[1].maxBackgroundDeltaUV > 3.0);
        }

        TEST_METHOD(ColorCorroborationNativeV210MatchesPlanarSamples)
        {
            P010Frame frame(384, 216, 16, true);
            frame.Fill(87, 510, 514);
            for (int y = 0; y < 216; ++y)
                for (int x = 0; x < 384; ++x)
                    WriteCode(frame.bytes.data() + y * frame.pitch + x * 2,
                        84 + ((x * 13 + y * 7) % 9));
            const auto planar = frame.P210Source();
            const size_t pitch = 384 / 6 * 16;
            std::vector<uint8_t> bytes(pitch * 216, 0);
            for (int y = 0; y < 216; ++y)
                for (int x = 0; x < 384; x += 6)
                {
                    AnalysisLumaSample p[6];
                    for (int i = 0; i < 6; ++i) Assert::IsTrue(planar.Sample(x + i, y, p[i]));
                    const uint32_t words[] = {
                        uint32_t(p[0].chromaU) | uint32_t(p[0].luma) << 10 | uint32_t(p[0].chromaV) << 20,
                        uint32_t(p[1].luma) | uint32_t(p[2].chromaU) << 10 | uint32_t(p[2].luma) << 20,
                        uint32_t(p[2].chromaV) | uint32_t(p[3].luma) << 10 | uint32_t(p[4].chromaU) << 20,
                        uint32_t(p[4].luma) | uint32_t(p[4].chromaV) << 10 | uint32_t(p[5].luma) << 20 };
                    auto target = bytes.data() + y * pitch + x / 6 * 16;
                    for (int word = 0; word < 4; ++word)
                        for (int b = 0; b < 4; ++b)
                            target[word * 4 + b] = static_cast<uint8_t>(words[word] >> (b * 8));
                }
            AnalysisLumaSource native{ bytes.data(), bytes.size(), 384, 216, pitch, 0,
                AnalysisLumaFormat::NativeYuv422, VideoFrameEncoding::V210, ColorSpace::REC_709, 1 };
            const auto a = EvaluateFullRasterColorEvidence(planar);
            const auto b = EvaluateFullRasterColorEvidence(native);
            Assert::IsTrue(a.evaluated && b.evaluated);
            Assert::AreEqual(a.candidateSupported, b.candidateSupported);
            for (int edge = 0; edge < 4; ++edge)
            {
                Assert::AreEqual(a.edges[edge].medianY, b.edges[edge].medianY);
                Assert::AreEqual(a.edges[edge].dispersionY, b.edges[edge].dispersionY);
                Assert::AreEqual(a.edges[edge].maxBackgroundDeltaUV, b.edges[edge].maxBackgroundDeltaUV);
            }
        }

        TEST_METHOD(ColorCandidateCannotDistinguishNearIdenticalNoisyBars)
        {
            // Deliberately document the counterexample rather than tuning a
            // threshold to this one recording: near-identical tinted noise in
            // encoded bars has the same weak features as picture. Shadow only.
            P010Frame frame(384, 216);
            frame.Fill(87, 510, 514);
            for (int y = 0; y < 216; ++y)
                for (int x = 0; x < 384; ++x)
                {
                    const bool encodedBar = y < 20 || y >= 196;
                    WriteCode(frame.bytes.data() + y * frame.pitch + x * 2,
                        (encodedBar ? 82 : 84) + ((x * 13 + y * 7) % 9));
                }
            auto source = frame.P010Source();
            source.encoding = VideoFrameEncoding::V210;
            const auto evidence = EvaluateFullRasterColorEvidence(source);
            Assert::IsTrue(evidence.evaluated);
            Assert::IsTrue(evidence.candidateSupported,
                L"Positive weak candidate is explicitly not geometry authority.");
        }

	};
}
