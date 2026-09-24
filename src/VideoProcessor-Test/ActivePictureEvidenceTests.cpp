#include "pch.h"

#include <ActivePictureEvidence.h>
#include <ActivePictureDecisionTimeline.h>
#include <RendererCropHandoff.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include "CppUnitTest.h"

#include <vector>

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
	}

	TEST_CLASS(ActivePictureEvidenceTests)
	{
	public:
		TEST_METHOD(DisplayHandoffRecoversVerifiedScopeFromAmbiguousDarkPixels)
		{
			P010Frame frame(960, 540);
			frame.Fill(64, 512, 512);
			// The picture's dark upper/left area obscures the true scope edge.
			frame.FillRectangle(266, 144, 960, 472, 120);
			const auto source = frame.P010Source();
			const auto scope = ScopePresentation(960, 540, 68, 472);
			RendererCropHandoff hint{ scope, 42, 7, source.generation, 100 };
			ActivePicturePresentationRetentionEvidence proof;
			Assert::IsTrue(CanRestoreRendererCrop(hint, source, 42, 7, 110, proof));
			Assert::IsTrue(proof.activePicture.classification == ActivePictureClassification::PROVISIONAL);
			Assert::IsTrue(proof.excludedBandsPixelSafe);
			ActivePictureTransitionModel replacement;
			ActivePictureTransitionDecision seed;
			seed.bounds = scope;
			seed.publish = seed.stable = true;
			Assert::IsTrue(replacement.AdoptPublishedDecision(seed,
				ActivePictureClassification::BAR_CROP_TRUSTED));
			const auto retained = ConstrainNearBlackGeometryChange(proof, scope);
			for (uint64_t sequence = 1; sequence <= 180; ++sequence)
			{
				const auto decision = replacement.Observe(
					MakeActivePictureObservation(retained, sequence, 23.976));
				Assert::IsTrue(decision.stable);
				Assert::IsFalse(decision.clearTransition);
				Assert::AreEqual(scope.top, decision.stableBounds.top);
				Assert::AreEqual(scope.bottom, decision.stableBounds.bottom);
			}
		}

		TEST_METHOD(DisplayHandoffRejectsChangedContextAndExpiredHint)
		{
			P010Frame frame(960, 540);
			frame.BlackOutside(0, 68, 960, 472);
			auto source = frame.P010Source();
			RendererCropHandoff hint{ ScopePresentation(960, 540, 68, 472), 42, 7, 1, 100 };
			ActivePicturePresentationRetentionEvidence proof;
			Assert::IsFalse(CanRestoreRendererCrop(hint, source, 43, 7, 110, proof));
			Assert::IsFalse(CanRestoreRendererCrop(hint, source, 42, 8, 110, proof));
			Assert::IsFalse(CanRestoreRendererCrop(hint, source, 42, 7, 10101, proof));
			Assert::IsFalse(CanRestoreRendererCrop(hint, source, 42, 7, 99, proof));
			source.generation = 2;
			Assert::IsFalse(CanRestoreRendererCrop(hint, source, 42, 7, 110, proof));
			source.generation = 1;
			hint.bounds.rasterWidth = 1920;
			Assert::IsFalse(CanRestoreRendererCrop(hint, source, 42, 7, 110, proof));
		}

		TEST_METHOD(DisplayHandoffRejectsLivePictureAndOverlayInExcludedBands)
		{
			P010Frame frame(960, 540);
			RendererCropHandoff hint{ ScopePresentation(960, 540, 68, 472), 42, 7, 1, 100 };
			ActivePicturePresentationRetentionEvidence proof;
			Assert::IsFalse(CanRestoreRendererCrop(hint, frame.P010Source(), 42, 7, 110, proof));
			frame.BlackOutside(0, 68, 960, 472);
			frame.FillRectangle(250, 490, 710, 520, 650);
			Assert::IsFalse(CanRestoreRendererCrop(hint, frame.P010Source(), 42, 7, 110, proof));
			frame.BlackOutside(0, 68, 960, 472, 64, 650, 512);
			Assert::IsFalse(CanRestoreRendererCrop(hint, frame.P010Source(), 42, 7, 110, proof));
		}


		TEST_METHOD(FailedOrthogonalBarCannotPublishInventedFormatFromRealPixels)
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
						Assert::IsFalse(decision.publish,L"Incomplete axes published an invented program aspect");
						Assert::AreEqual(base.top,decision.stableBounds.top);
						Assert::AreEqual(base.left,decision.stableBounds.left);
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
				// Provisional history lookup must obey the same current failed-axis veto.
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
			Assert::AreEqual(280, evidence.trustedBounds.top);
			Assert::AreEqual(1880, evidence.trustedBounds.bottom);
			Assert::IsTrue(evidence.lumaSamples < 30000);
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
