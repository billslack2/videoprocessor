#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <vprenderer/AlphaQueuePolicy.h>
#include <deque>
#include <array>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
	namespace
	{
		ActivePictureBounds BufferedScope()
		{
			return {0,276,3840,1884,3840,2160,3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
		}
		ActivePictureBounds BufferedTaller()
		{
			return {0,68,3840,2092,3840,2160,3840.0/2024.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
		}
		ActivePictureFrameIdentity BufferedIdentity(uint64_t sequence)
		{
			return {7,sequence,sequence,sequence*417083,11,13,17};
		}
		ActivePictureObservation BufferedObservation(uint64_t sequence,const ActivePictureBounds& bounds)
		{
			return {bounds,sequence,true,ActivePictureClassification::BAR_CROP_TRUSTED,24};
		}
		struct BufferedPixelSample
		{
			std::vector<uint16_t> pixels;
			ActivePictureEvidence evidence;
			ActivePicturePresentationRetentionEvidence retention;
			BufferedPixelSample(int top=68,int bottom=2092) : pixels(3840*2160*3/2,uint16_t(512<<6))
			{
				for (int y=0;y<2160;++y)
					if (y<top || y>=bottom)
						std::fill(pixels.begin()+size_t(y)*3840,pixels.begin()+size_t(y+1)*3840,uint16_t(64<<6));
				AnalysisLumaSource source;
				source.data=reinterpret_cast<const uint8_t*>(pixels.data()); source.dataBytes=pixels.size()*sizeof(uint16_t);
				source.width=3840; source.height=2160; source.rowBytes=source.chromaRowBytes=3840*sizeof(uint16_t);
				source.format=AnalysisLumaFormat::P010; source.generation=11;
				evidence=ExtractActivePictureEvidence(source);
				retention=EvaluateActivePicturePresentationRetention(source,BufferedScope());
			}
			AnalysisLumaSource Source() const
			{
				AnalysisLumaSource source;
				source.data=reinterpret_cast<const uint8_t*>(pixels.data()); source.dataBytes=pixels.size()*sizeof(uint16_t);
				source.width=3840; source.height=2160; source.rowBytes=source.chromaRowBytes=3840*sizeof(uint16_t);
				source.format=AnalysisLumaFormat::P010; source.generation=7;
				return source;
			}
		};
		std::array<BufferedPictureExpansionSample,3> BufferedSamples(
			const std::array<BufferedPixelSample,3>& frames,uint64_t firstSequence=100)
		{
			std::array<BufferedPictureExpansionSample,3> samples;
			for (size_t index=0;index<samples.size();++index)
			{
				samples[index].identity=BufferedIdentity(firstSequence+index);
				samples[index].observation=MakeActivePictureObservation(frames[index].evidence,firstSequence+index,24);
				samples[index].retention=frames[index].retention;
				samples[index].nearBlackEvaluated=true;
			}
			return samples;
		}
		TransitionAdmissionInput BufferedLiveInput(const BufferedPixelSample& frame)
		{
			TransitionAdmissionInput input;
			input.evidence=frame.evidence;
			input.outwardCandidate=frame.evidence.trustedBounds;
			input.trustedGeometry=input.presentationBeforeObservation=BufferedScope();
			input.trustedGeometryAvailable=input.compatiblePresentation=true;
			input.trustedGeneration=input.sourceGeneration=input.presentationEvidenceGeneration=7;
			input.sourceSequence=100; input.framesPerSecond=24; input.retention=frame.retention;
			return input;
		}
		void EstablishBufferedScope(ActivePictureDecisionTimeline& timeline,ActivePictureTransitionModel& live)
		{
			for (uint64_t sequence=1;sequence<=4;++sequence)
			{
				ActivePictureFrameDecision initial;
				timeline.SubmitScheduledObservation(BufferedIdentity(sequence),BufferedObservation(sequence,BufferedScope()),0,0,initial);
				live.Observe(BufferedObservation(sequence,BufferedScope()));
			}
		}
		// Reuse the existing pixel/queue fixture through the same final recovery,
		// admission and screen-fill stages that can otherwise hide a crop step.
		struct BufferedMotionSequence
		{
			ActivePictureDecisionTimeline timeline;
			ActivePictureTransitionModel model;
			ActivePictureBounds geometry=BufferedScope();
			MovingPictureTransitionState moving;
			OutwardPictureConfirmationState outward;
			CropPresentationAdmissionState admitted;
			PresentationRecoveryState recovery;
			VerticalFitConfirmationState fit;
			VerticalBarPresentationState densePresentation;
            VerticalTranslationDrift translationDrift;
            ActivePictureBounds subtitleBase=BufferedScope();
			unsigned wouldBeDenseFits=0;
            bool enableMotion=true;
			Decision presented;
			ActivePictureBounds finalBounds;
			bool published=false;
			uint64_t sequence=100;

			BufferedMotionSequence() { EstablishBufferedScope(timeline,model); }
			void Step(const BufferedPixelSample& frame,
				const BufferedPixelSample* future1=nullptr,const BufferedPixelSample* future2=nullptr,
                uint8_t configuredLookahead=2,uint8_t availableLookahead=2,
                const ActivePictureFrameDecision* inwardProof=nullptr)
			{
				auto current=BufferedLiveInput(frame);
				current.sourceSequence=sequence;
				current.trustedGeometry=current.presentationBeforeObservation=geometry;
				current.retention=EvaluateActivePicturePresentationRetention(frame.Source(),geometry);
				current.previousOutward=outward;
				current.presentation=densePresentation;
                current.translationDriftActive=translationDrift.IsActive();
				const auto identity=BufferedIdentity(sequence);
                const bool previousMoving=moving.active || moving.awaitingPublication;
				if (enableMotion) moving=ObserveMovingPictureTransition(moving,identity,current);
				auto admission=EvaluateTransitionAdmission(current);
				ConstrainMovingPictureTransition(moving,admission);
				outward=admission.outward.state;
				const bool eligible=model.WouldAdmitGeometryChange(MakeActivePictureObservation(current.evidence,sequence,24));
                ActivePictureTransitionDecision transition;
                const bool inwardAccepted=inwardProof && !moving.active && !moving.awaitingPublication &&
                    current.presentation.action==VerticalBarPresentationAction::NONE &&
                    !current.translationDriftActive && !current.previousOutward.verticalPresentationSeen &&
                    !admission.deferOutward && !admission.deferPresentation &&
                    ValidateActivePictureScheduledDecision(*inwardProof,identity,current.evidence.trustedBounds,
                        current.evidence.classification)==ActivePictureScheduledDecisionValidation::ACCEPTED &&
                    model.AdoptPublishedDecision(inwardProof->transition,current.evidence.classification,
                        admission.observation.transitionDeferred,nullptr,&admission.observation.axisEvidence);
                if (inwardAccepted) transition=inwardProof->transition;
                else transition=model.Observe(admission.observation);
				if (!moving.active && future1 && future2)
				{
					const BufferedPixelSample* pixels[3]={&frame,future1,future2};
					std::array<BufferedPictureExpansionSample,3> samples;
					for (size_t index=0;index<3;++index)
					{
						samples[index].identity=BufferedIdentity(sequence+index);
						samples[index].observation=MakeActivePictureObservation(pixels[index]->evidence,sequence+index,24);
						samples[index].retention=EvaluateActivePicturePresentationRetention(pixels[index]->Source(),geometry);
						samples[index].nearBlackEvaluated=true;
					}
					const auto proof=BuildBufferedPictureExpansion(samples.data(),3,geometry,configuredLookahead,availableLookahead,19,23);
                    const bool ordinary=ValidateBufferedPictureExpansion(proof,identity,current,eligible);
                    const bool translated=!previousMoving &&
                        ValidateBufferedTranslatedPictureExpansion(proof,identity,current,eligible,subtitleBase);
                    const bool adopted=(ordinary || translated) &&
                        model.AdoptPublishedDecision(proof.decision.transition,current.evidence.classification,
                            false,nullptr,&admission.observation.axisEvidence,true);
                    if (adopted) transition=proof.decision.transition;
                    RetireVerticalPresentationForBufferedExpansion(adopted && translated,
                        densePresentation,translationDrift,outward);
				}
				CompleteMovingPictureTransition(moving,current,transition);
				published=transition.publish;
				const auto measuredBase=geometry;
				if (published) geometry=transition.bounds;
				const auto retained=ResolveActivePictureRetentionHandoff(frame.Source(),measuredBase,current.retention,geometry);
				Input crop;
				crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
				crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.latestObservationClassification=current.evidence.classification;
				crop.geometry=geometry; crop.rasterWidth=3840; crop.rasterHeight=2160;
				crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
				crop.frameSourceSequence=sequence; crop.framePresentationEpoch=13;
				crop.movingPictureTransition=HasCurrentMovingPictureTransition(moving,7,sequence);
				crop.latestObservationSupportsCrop=IsPixelSafeCropReaffirmation(geometry,
					current.evidence.trustedBounds,retained.evidence.excludedBandsPixelSafe);
				crop.barCropRefinementPending=!crop.latestObservationSupportsCrop;
				crop.frameLocalPresentationRetentionEvaluated=true;
				crop.frameLocalPresentationRetentionSafe=retained.evidence.CanRetainPresentation();
				crop.pictureTransitionHandoff=MakePictureTransitionHandoff(current,admission,transition,eligible,13);
                crop.verticalTranslationActive=densePresentation.action==VerticalBarPresentationAction::TRANSLATE;
                crop.verticalTranslationPixels=crop.verticalTranslationActive ? static_cast<int>(densePresentation.translationPixels) : 0;
                crop.verticalTranslationBase=subtitleBase;
                crop.verticalTranslationSourceGeneration=7;
				PresentationRecoveryInput recover;
				recover.previous=recovery; recover.crop=crop; recover.candidate=Evaluate(crop);
				recover.measurementCurrent=recover.retentionEvaluated=recover.nearBlackEvaluated=true;
				recover.retentionBounds=retained.bounds; recover.retentionSourceGeneration=7;
				recover.retentionSourceSequence=sequence;
				recover.observationAvailable=retained.evidence.proposedBoundsAvailable;
				recover.observation=retained.evidence.activePicture.proposedBounds;
				recover.observedTrustedCrop=retained.evidence.activePicture.trustedBounds;
				recover.observationClassification=retained.evidence.activePicture.classification;
				recover.excludedBandsPixelSafe=retained.evidence.excludedBandsPixelSafe;
				recover.presentationEpoch=13; recover.currentTick=sequence*42; recover.framesPerSecond=24;
				const auto recovered=EvaluatePresentationRecovery(recover); recovery=recovered.state;
				const auto visible=AdmitCropPresentation(admitted,crop,recovered.presentation,13);
				admitted=visible.state; presented=visible.presentation;
				AspectLimitFillInput fill;
				fill.sourceBounds=presented.sourceBounds;
				fill.trustedContentAuthorityAccepted=presented.applyCrop;
				fill.cropWiderContentToFillScreen=fill.widerLimitConfigured=true;
				fill.widerAspectLimit=2.41; fill.screenAspect=2.35;
				finalBounds=EvaluateAspectLimitFill(fill).sourceBounds;
				// Dense subtitle analysis scans the retained scope, so the newly
				// revealed real pixels would eventually confirm a competing FIT.
				VerticalBarContentInput bars;
				bars.upperOccupiedDepth=(std::max)(0,geometry.top-frame.evidence.trustedBounds.top);
				bars.lowerOccupiedDepth=(std::max)(0,frame.evidence.trustedBounds.bottom-geometry.bottom);
				bars.upperContent=bars.upperOccupiedDepth>0; bars.lowerContent=bars.lowerOccupiedDepth>0;
				bars.upperBarPixels=geometry.top; bars.lowerBarPixels=2160-geometry.bottom;
				bars.upperPeakSamples=bars.lowerPeakSamples=bars.sampledColumns=1920;
				const auto detected=EvaluateVerticalBarContent(bars);
				if (detected.action==VerticalBarPresentationAction::FIT) ++wouldBeDenseFits;
				if (HasCurrentMovingPictureTransition(moving,7,sequence))
				{
					fit={}; densePresentation={};
				}
				else
				{
					const auto confirmed=ConfirmVerticalFit(fit,detected,sequence); fit=confirmed.state;
					VerticalBarPresentationUpdateInput update;
					update.previous=densePresentation; update.current=confirmed.effective;
					update.upperContent=bars.upperContent; update.lowerContent=bars.lowerContent;
					update.upperContentTop=frame.evidence.trustedBounds.top;
					update.lowerContentBottom=frame.evidence.trustedBounds.bottom;
					update.currentTick=sequence*42; update.currentSourceSequence=sequence;
					update.translationEnabled=true;
					densePresentation=UpdateVerticalBarPresentation(update);
				}
				++sequence;
			}
		};
	}
	TEST_CLASS(LookaheadOutwardProofTests)
	{
	public:
        TEST_METHOD(BufferedBroadExpansionRetiresTranslationOnFirstFrameAndKeepsFinalCrop)
        {
            const BufferedPixelSample taller;
            for (bool driftActive : {false,true})
            {
                BufferedMotionSequence replay;
                auto base=BufferedScope(); base.top=272; base.aspectRatio=3840.0/(1884-272);
                replay.geometry=replay.subtitleBase=base;
                replay.model.Reset();
                for (uint64_t sequence=1;sequence<=4;++sequence)
                    replay.model.Observe(BufferedObservation(sequence,base));
                replay.densePresentation.action=VerticalBarPresentationAction::TRANSLATE;
                replay.densePresentation.translationPixels=178;
                replay.densePresentation.sourceSequence=99;
                replay.outward.verticalPresentationSeen=true;
                replay.outward.sourceGeneration=7;
                replay.translationDrift.Resolve(178,4000,0);
                if (driftActive) replay.translationDrift.Resolve(0,4100,500);

                Input before;
                before.automaticCropEnabled=before.sharedGeometryAvailable=before.latestObservationSupportsCrop=true;
                before.classification=before.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
                before.geometry=before.verticalTranslationBase=base;
                before.geometrySourceGeneration=before.frameSourceGeneration=before.verticalTranslationSourceGeneration=7;
                before.frameSourceSequence=99; before.framePresentationEpoch=13;
                before.rasterWidth=3840; before.rasterHeight=2160;
                before.verticalTranslationActive=true; before.verticalTranslationPixels=178;
                const auto translated=Evaluate(before);
                Assert::IsTrue(translated.applyCrop && translated.verticallyTranslated);
                AspectLimitFillInput initialFill;
                initialFill.sourceBounds=translated.sourceBounds; initialFill.trustedContentAuthorityAccepted=true;
                initialFill.cropWiderContentToFillScreen=initialFill.widerLimitConfigured=true;
                initialFill.widerAspectLimit=2.41; initialFill.screenAspect=2.35;
                const auto initial=EvaluateAspectLimitFill(initialFill).sourceBounds;
                Assert::AreEqual(26,initial.left); Assert::AreEqual(3814,initial.right);
                Assert::AreEqual(450,initial.top); Assert::AreEqual(2062,initial.bottom);

                replay.Step(taller,&taller,&taller);
                Assert::IsTrue(replay.published,L"Three buffered broad picture samples must take over the prior translated scope on the first source frame.");
                for (unsigned following=0;following<6;++following)
                {
                    Assert::IsTrue(replay.presented.applyCrop);
                    Assert::IsFalse(replay.presented.verticallyTranslated);
                    Assert::AreEqual(0,replay.presented.verticalTranslationPixels);
                    Assert::AreEqual(0,replay.finalBounds.left); Assert::AreEqual(3840,replay.finalBounds.right);
                    Assert::AreEqual(68,replay.finalBounds.top); Assert::AreEqual(2092,replay.finalBounds.bottom);
                    Assert::IsFalse(replay.recovery.active);
                    Assert::IsFalse(replay.translationDrift.IsActive());
                    Assert::IsFalse(replay.outward.verticalPresentationSeen);
                    Assert::IsTrue(replay.densePresentation.action==VerticalBarPresentationAction::NONE);
                    replay.Step(taller,&taller,&taller);
                    Assert::IsFalse(replay.published,L"Retired subtitle ownership cannot restart the old framing after accepted expansion.");
                }
            }
        }

        TEST_METHOD(TranslatedBufferedExpansionKeepsGenerationBaseOwnerAndCurrentPixelVetoes)
        {
            const std::array<BufferedPixelSample,3> frames;
            const auto samples=BufferedSamples(frames);
            const auto proof=BuildBufferedPictureExpansion(samples.data(),samples.size(),BufferedScope(),2,2,19,23);
            Assert::IsTrue(proof.valid);
            for (int fault=0;fault<14;++fault)
            {
                auto current=BufferedLiveInput(frames[0]);
                current.presentation.action=VerticalBarPresentationAction::TRANSLATE;
                current.presentation.translationPixels=178;
                current.previousOutward.verticalPresentationSeen=true;
                auto identity=BufferedIdentity(100);
                auto subtitleBase=BufferedScope();
                bool eligible=true;
                switch (fault)
                {
                case 0: current.presentation.action=VerticalBarPresentationAction::NONE; break;
                case 1: current.presentation.action=VerticalBarPresentationAction::FIT; break;
                case 2: current.presentationEvidenceGeneration=0; break;
                case 3: ++current.presentationEvidenceGeneration; break;
                case 4: subtitleBase.top+=4; break;
                case 5: ++current.sourceGeneration; break;
                case 6: ++identity.sourceFrameNumber; break;
                case 7: current.retention.globalNearBlack=true; break;
                case 8: current.retention.expansionStripsAvailable=false; break;
                case 9: current.evidence.classification=ActivePictureClassification::PROVISIONAL; break;
                case 10: current.evidence.trustedBounds.top+=4; break;
                case 11: current.presentationBeforeObservation.top+=4; break;
                case 12: eligible=false; break;
                case 13: current.sourceSequence+=1; break;
                }
                const auto diagnostic=L"fault="+std::to_wstring(fault);
                Assert::IsFalse(ValidateBufferedTranslatedPictureExpansion(proof,identity,current,eligible,subtitleBase),diagnostic.c_str());
            }
        }

        TEST_METHOD(SparseSubtitleAndBottomMenuPixelsCannotTakeTranslatedScopeOwnership)
        {
            for (bool menu : {false,true})
            {
                std::array<BufferedPixelSample,3> frames={BufferedPixelSample(276,1884),BufferedPixelSample(276,1884),BufferedPixelSample(276,1884)};
                for (auto& frame : frames)
                {
                    const int left=menu ? 0 : 1400, right=menu ? 3840 : 2400;
                    const int top=menu ? 1884 : 1960, bottom=menu ? 2092 : 2000;
                    for (int y=top;y<bottom;++y)
                        std::fill(frame.pixels.begin()+size_t(y)*3840+left,
                            frame.pixels.begin()+size_t(y)*3840+right,uint16_t(512<<6));
                    frame.evidence=ExtractActivePictureEvidence(frame.Source());
                    frame.retention=EvaluateActivePicturePresentationRetention(frame.Source(),BufferedScope());
                }
                const auto samples=BufferedSamples(frames);
                const auto proof=BuildBufferedPictureExpansion(samples.data(),samples.size(),BufferedScope(),2,2,19,23);
                Assert::IsFalse(proof.valid,L"One-sided subtitle/menu pixels are not a broad opposing picture expansion.");
                auto current=BufferedLiveInput(frames[0]);
                current.presentation.action=VerticalBarPresentationAction::TRANSLATE;
                current.presentation.translationPixels=178;
                current.previousOutward.verticalPresentationSeen=true;
                Assert::IsFalse(ValidateBufferedTranslatedPictureExpansion(proof,BufferedIdentity(100),current,true,BufferedScope()));
            }
        }

        TEST_METHOD(PreviousMovingEpisodeCannotTakeOverTranslationAfterCurrentObservationClearsMotion)
        {
            const BufferedPixelSample taller;
            for (bool awaitingPublication : {false,true})
            {
                BufferedMotionSequence replay;
                replay.densePresentation.action=VerticalBarPresentationAction::TRANSLATE;
                replay.densePresentation.translationPixels=178;
                replay.densePresentation.sourceSequence=99;
                replay.outward.verticalPresentationSeen=true;
                replay.outward.sourceGeneration=7;
                replay.moving.active=!awaitingPublication;
                replay.moving.awaitingPublication=awaitingPublication;
                replay.moving.identity=BufferedIdentity(99);
                replay.moving.base=BufferedScope();
                replay.Step(taller,&taller,&taller);
                Assert::IsFalse(replay.published,
                    L"Previous active/awaiting movement must veto subtitle takeover before current translation clears the motion tracker.");
                Assert::AreEqual(276,replay.geometry.top);
                Assert::AreEqual(1884,replay.geometry.bottom);
            }
        }

        TEST_METHOD(TranslationRetirementRequiresSuccessfulAdoptionAndClearsReleaseDrift)
        {
            for (bool adopted : {false,true})
            {
                VerticalBarPresentationState presentation;
                presentation.action=VerticalBarPresentationAction::TRANSLATE;
                presentation.translationPixels=178; presentation.sourceSequence=99;
                VerticalTranslationDrift drift;
                drift.Resolve(178,4000,0); drift.Resolve(0,4100,500);
                Assert::IsTrue(drift.IsActive());
                OutwardPictureConfirmationState outward;
                outward.verticalPresentationSeen=true; outward.confirmations=2; outward.sourceGeneration=7;
                RetireVerticalPresentationForBufferedExpansion(adopted,presentation,drift,outward);
                if (!adopted)
                {
                    Assert::IsTrue(presentation.action==VerticalBarPresentationAction::TRANSLATE);
                    Assert::AreEqual(178.0f,presentation.translationPixels);
                    Assert::IsTrue(drift.IsActive());
                    Assert::IsTrue(outward.verticalPresentationSeen);
                    Assert::AreEqual(2u,outward.confirmations);
                }
                else
                {
                    Assert::IsTrue(presentation.action==VerticalBarPresentationAction::NONE);
                    Assert::AreEqual(0.0f,presentation.translationPixels);
                    Assert::IsFalse(drift.IsActive());
                    Assert::AreEqual(0.0f,drift.Resolve(0,4200,500));
                    Assert::IsFalse(drift.ConsumeFinalBaseFrame());
                    Assert::IsFalse(outward.verticalPresentationSeen);
                    Assert::AreEqual(0u,outward.confirmations);
                }
            }
        }

        TEST_METHOD(BufferedInwardUsesAvailableAdjacentProofAtLiveCropCadence)
        {
            for (double fps : {24.0, 60.0})
            {
                ActivePictureTransitionModel live;
                for (uint64_t sequence=1; sequence<=4; ++sequence)
                    live.Observe(BufferedObservation(sequence,BufferedTaller()));
                std::array<BufferedPictureExpansionSample,2> samples;
                for (size_t i=0; i<samples.size(); ++i)
                {
                    samples[i].identity=BufferedIdentity(100+i);
                    samples[i].observation=BufferedObservation(100+i,BufferedScope());
                    samples[i].observation.framesPerSecond=fps;
                    samples[i].nearBlackEvaluated=true;
                }
                const auto proof=BuildBufferedInwardDecision(samples.data(),samples.size(),live,
                    BufferedTaller(),1,1,19,23);
                Assert::IsTrue(proof.transition.publish,
                    L"Two queued exact inward samples must confirm at established live-crop cadence.");
                Assert::AreEqual(uint64_t{100},proof.effectiveIdentity.acceptedSequence);
                Assert::AreEqual(uint64_t{101},proof.observationIdentity.acceptedSequence);
                Assert::AreEqual(uint8_t{2},proof.proofFrameCount);
                Assert::IsTrue(live.AdoptPublishedDecision(proof.transition,
                    ActivePictureClassification::BAR_CROP_TRUSTED));
            }
        }
		TEST_METHOD(MotionReturnInsideDeadbandReusesOnlyPixelSafePriorCrop)
		{
			BufferedMotionSequence replay;
			for (int top=268;top>=120;top-=4)
			{
				BufferedPixelSample moving(top,2160-top); replay.Step(moving);
			}
			Assert::IsTrue(replay.moving.active);
			// The new bars are four pixels farther inward on both edges. The
			// original crop excludes only black and needs no new publication.
			BufferedPixelSample nearBase(280,1880);
			for (unsigned offset=0;offset<8;++offset)
			{
				replay.Step(nearBase);
				Assert::IsFalse(replay.moving.active);
				Assert::IsFalse(replay.moving.awaitingPublication);
				Assert::IsFalse(replay.published,L"Safe return within the existing deadband must not require new geometry authority.");
				Assert::IsTrue(replay.presented.applyCrop);
				Assert::AreEqual(276,replay.finalBounds.top);
				Assert::AreEqual(1884,replay.finalBounds.bottom);
			}
		}
		TEST_METHOD(LocalMotionSettlementAndHardCutKeepFullUntilActualPublication)
		{
			for (bool hardCut : {false,true})
			{
				BufferedMotionSequence replay;
				for (int top=268;top>=120;top-=4)
				{
					BufferedPixelSample moving(top,2160-top); replay.Step(moving);
				}
				Assert::IsTrue(replay.moving.active);
				const int targetTop=hardCut ? 68 : 120;
				BufferedPixelSample target(targetTop,2160-targetTop);
				unsigned publications=0;
				bool acquired=false,quietExitSeen=false;
				for (unsigned offset=0;offset<24;++offset)
				{
					replay.Step(target);
                    if (quietExitSeen) Assert::IsFalse(replay.moving.active,
                        L"Identical settled pixels cannot restart movement while local publication is pending.");
                    if (replay.moving.awaitingPublication) quietExitSeen=true;
					if (replay.published)
					{
						++publications; acquired=true;
						if (hardCut) Assert::AreEqual(3u,offset,L"Local hard-cut publication timing must remain unchanged.");
					}
					if (!acquired)
					{
						Assert::IsFalse(replay.presented.applyCrop,L"Motion cannot flash back to the old scope while final acquisition is pending.");
						Assert::AreEqual(0,replay.finalBounds.top);
						Assert::AreEqual(2160,replay.finalBounds.bottom);
					}
					else
					{
						Assert::IsTrue(replay.presented.applyCrop);
						Assert::AreEqual(targetTop,replay.finalBounds.top);
						Assert::AreEqual(2160-targetTop,replay.finalBounds.bottom);
					}
				}
				Assert::AreEqual(1u,publications,hardCut ? L"hard cut" : L"quiet settlement");
			}
		}
		TEST_METHOD(DisabledMotionControlReproducesIntermediateCropSteps)
		{
			BufferedMotionSequence baseline; baseline.enableMotion=false;
			unsigned publications=0,visibleSizeChanges=0;
			int previousHeight=0;
			for (int top=268;top>=120;top-=2)
			{
				BufferedPixelSample current(top,2160-top),next(top-2,2162-top),later(top-4,2164-top);
				baseline.Step(current,&next,&later);
				if (baseline.published) ++publications;
				const int height=baseline.finalBounds.bottom-baseline.finalBounds.top;
				if (previousHeight!=0 && previousHeight!=height) ++visibleSizeChanges;
				previousHeight=height;
			}
			Assert::IsTrue(publications>=3,L"Disabled-motion control must reproduce repeated intermediate crop publications.");
			Assert::IsTrue(visibleSizeChanges>=3,L"The reproduction must reach final presentation, not only detector flags.");
		}
		TEST_METHOD(ActualMovingPixelsDoNotProduceBufferedPresentationStaircase)
		{
			for (int step : {2,4})
			{
				BufferedMotionSequence replay;
				bool entered=false;
				unsigned movingFrames=0;
				// Stops at an intermediate aspect: full-frame is a temporary
				// presentation choice, never a guessed 16:9 detection result.
				for (int top=268;top>=120;top-=step)
				{
					BufferedPixelSample current(top,2160-top);
					BufferedPixelSample next(top-step,2160-top+step);
					BufferedPixelSample later(top-2*step,2160-top+2*step);
					replay.Step(current,&next,&later);
					entered=entered || replay.moving.active;
					if (entered)
					{
						++movingFrames;
						Assert::IsTrue(replay.moving.active);
						Assert::IsFalse(replay.published,L"Moving bars cannot publish intermediate formats.");
						Assert::IsFalse(replay.presented.applyCrop);
						Assert::AreEqual(0,replay.finalBounds.top);
						Assert::AreEqual(2160,replay.finalBounds.bottom);
						Assert::AreEqual(0,replay.finalBounds.left);
						Assert::AreEqual(3840,replay.finalBounds.right);
					}
				}
				Assert::IsTrue(entered && movingFrames>10,L"Actual slow picture growth must be recognized.");
                Assert::IsTrue(replay.wouldBeDenseFits>1,L"The ramp must exercise real dense FIT feedback.");
                Assert::IsTrue(replay.densePresentation.action==VerticalBarPresentationAction::NONE);
				BufferedPixelSample settled(120,2040);
				unsigned finalAcquisitions=0;
				bool finalCrop=false;
				for (int frame=0;frame<24;++frame)
				{
					replay.Step(settled,&settled,&settled);
					if (replay.published) ++finalAcquisitions;
					if (replay.presented.applyCrop)
					{
						finalCrop=true;
						Assert::AreEqual(120,replay.finalBounds.top);
						Assert::AreEqual(2040,replay.finalBounds.bottom);
					}
					else Assert::IsFalse(finalCrop,L"Settled picture must not alternate crop and full-frame.");
				}
				Assert::IsFalse(replay.moving.active);
				Assert::IsTrue(finalCrop,L"An intermediate final aspect must reacquire within one second.");
				Assert::AreEqual(1u,finalAcquisitions);
				// Inject a normal hard expansion after this movement episode.
				BufferedPixelSample hard(68,2092);
				replay.Step(hard,&hard,&hard);
				Assert::IsFalse(replay.moving.active);
				Assert::IsTrue(replay.published,L"Previous motion must not delay buffered hard changes.");
				Assert::IsTrue(replay.presented.applyCrop);
				Assert::AreEqual(68,replay.finalBounds.top);
				Assert::AreEqual(2092,replay.finalBounds.bottom);
			}
		}

		TEST_METHOD(BufferedHardChangeKeepsFirstFrameTimingBeforeAndDuringMovement)
		{
			for (bool injectMovement : {false,true})
			{
				BufferedMotionSequence replay;
				if (injectMovement)
				{
					for (int top=268;top>=232;top-=4)
					{
						BufferedPixelSample frame(top,2160-top);
						replay.Step(frame);
					}
					Assert::IsTrue(replay.moving.active);
				}
				BufferedPixelSample hard(68,2092);
				replay.Step(hard,&hard,&hard);
				Assert::IsFalse(replay.moving.active);
				Assert::IsTrue(replay.published);
				Assert::IsTrue(replay.presented.applyCrop);
				Assert::AreEqual(68,replay.finalBounds.top);
				Assert::AreEqual(2092,replay.finalBounds.bottom);
			}
		}
        TEST_METHOD(SelectedPhysicalInwardProofReachesFirstFrameFinalCropAndRemainsStable)
        {
            struct Frame { bool cadenceRepeat; const BufferedPixelSample* pixels; };
            const BufferedPixelSample scope(276,1884);
            for (size_t depth : {size_t{0},size_t{1},size_t{2},size_t{3},size_t{5},size_t{8}})
            {
                BufferedMotionSequence replay;
                replay.model.Reset();
                for (uint64_t sequence=1;sequence<=4;++sequence)
                    replay.model.Observe(BufferedObservation(sequence,BufferedTaller()));
                replay.geometry=BufferedTaller();
                unsigned publications=0;
                unsigned firstPublication=99;
                for (unsigned offset=0;offset<5;++offset)
                {
                    std::deque<Frame> queue(9,Frame{false,&scope});
                    const auto window=AlphaQueuePolicy::SelectActivePicturePreview(queue,depth,8);
                    std::vector<BufferedPictureExpansionSample> samples;
                    for (size_t selected=0;selected<window.indices.size();++selected)
                    {
                        const auto& pixels=*queue[window.indices[selected]].pixels;
                        BufferedPictureExpansionSample sample;
                        sample.identity=BufferedIdentity(replay.sequence+selected);
                        sample.observation=MakeActivePictureObservation(pixels.evidence,replay.sequence+selected,24);
                        const auto nearBlack=EvaluateActivePictureGlobalNearBlack(pixels.Source());
                        sample.nearBlackEvaluated=nearBlack.evaluated;
                        sample.retention.globalNearBlack=nearBlack.nearBlack;
                        samples.push_back(sample);
                    }
                    const auto proof=BuildBufferedInwardDecision(samples.data(),samples.size(),replay.model,
                        replay.geometry,static_cast<uint8_t>(depth),static_cast<uint8_t>(window.availableFutureFrames),19,23);
                    replay.Step(scope,nullptr,nullptr,static_cast<uint8_t>(depth),
                        static_cast<uint8_t>(window.availableFutureFrames),proof.transition.publish ? &proof : nullptr);
                    if (replay.published && publications++==0) firstPublication=offset;
                    if (publications!=0)
                    {
                        Assert::IsTrue(replay.presented.applyCrop);
                        Assert::AreEqual(276,replay.finalBounds.top);
                        Assert::AreEqual(1884,replay.finalBounds.bottom);
                        Assert::IsFalse(replay.recovery.active);
                    }
                }
                Assert::AreEqual(1u,publications);
                Assert::AreEqual(depth>0 ? 0u : 1u,firstPublication);
            }
        }

        TEST_METHOD(SelectedQueueDepthControlsFirstDisplayedExpansionWithoutChangingFinalCrop)
        {
            struct Frame { bool cadenceRepeat; const BufferedPixelSample* pixels; };
            const BufferedPixelSample target;
            for (size_t depth : {size_t{0},size_t{1},size_t{2},size_t{3},size_t{5},size_t{8}})
            for (size_t actualFuture : {size_t{0},size_t{1},size_t{2},size_t{8}})
            {
                BufferedMotionSequence replay;
                unsigned publications=0;
                unsigned firstPublication=99;
                for (unsigned offset=0;offset<8;++offset)
                {
                    std::deque<Frame> queue={{false,&target}};
                    for (size_t future=0;future<actualFuture;++future)
                    {
                        queue.push_back({true,&target}); // repeated display adds no source evidence
                        queue.push_back({false,&target});
                    }
                    const auto window=AlphaQueuePolicy::SelectActivePicturePreview(queue,depth,8);
                    Assert::AreEqual(actualFuture,window.availableFutureFrames);
                    const auto* future1=window.indices.size()>1 ? queue[window.indices[1]].pixels : nullptr;
                    const auto* future2=window.indices.size()>2 ? queue[window.indices[2]].pixels : nullptr;
                    replay.Step(target,future1,future2,static_cast<uint8_t>(depth),
                        static_cast<uint8_t>(window.availableFutureFrames));
                    if (replay.published)
                    {
                        if (publications++==0) firstPublication=offset;
                    }
                    if (publications!=0)
                    {
                        Assert::IsTrue(replay.presented.applyCrop);
                        Assert::AreEqual(68,replay.finalBounds.top);
                        Assert::AreEqual(2092,replay.finalBounds.bottom);
                    }
                }
                const auto diagnostic=L"depth="+std::to_wstring(depth)+L" available="+std::to_wstring(actualFuture);
                Assert::AreEqual(1u,publications,diagnostic.c_str());
                Assert::AreEqual(depth>=2 && actualFuture>=2 ? 0u : 3u,firstPublication,diagnostic.c_str());
            }
        }

        TEST_METHOD(DeepSelectedQueueCannotPresentExpansionBeforeItsSourceFrameOrReorderReturn)
        {
            struct Frame { bool cadenceRepeat; const BufferedPixelSample* pixels; };
            const BufferedPixelSample scope(276,1884),taller;
            const std::vector<const BufferedPixelSample*> source={
                &scope,&scope,&taller,&taller,&taller,&taller,
                &scope,&scope,&scope,&scope,&scope,&scope};
            for (size_t depth : {size_t{0},size_t{1},size_t{2},size_t{3},size_t{5},size_t{8}})
            {
                BufferedMotionSequence replay;
                std::vector<int> publishedTop;
                for (size_t index=0;index<source.size();++index)
                {
                    std::deque<Frame> queue;
                    for (size_t pending=index;pending<source.size();++pending)
                        queue.push_back({false,source[pending]});
                    const auto window=AlphaQueuePolicy::SelectActivePicturePreview(queue,depth,8);
                    const auto* future1=window.indices.size()>1 ? queue[window.indices[1]].pixels : nullptr;
                    const auto* future2=window.indices.size()>2 ? queue[window.indices[2]].pixels : nullptr;
                    replay.Step(*source[index],future1,future2,static_cast<uint8_t>(depth),
                        static_cast<uint8_t>(window.availableFutureFrames));
                    if (replay.published) publishedTop.push_back(replay.geometry.top);
                    if (index<2)
                    {
                        Assert::IsTrue(replay.presented.applyCrop);
                        Assert::AreEqual(276,replay.finalBounds.top,
                            L"A later buffered expansion cannot affect preceding scope source frames.");
                    }
                }
                Assert::AreEqual<size_t>(2,publishedTop.size());
                Assert::AreEqual(68,publishedTop[0]);
                Assert::AreEqual(276,publishedTop[1]);
                Assert::IsTrue(replay.presented.applyCrop);
                Assert::AreEqual(276,replay.finalBounds.top);
                Assert::AreEqual(1884,replay.finalBounds.bottom);
            }
        }

		TEST_METHOD(PhysicalBufferedSamplesProvideThreeDistinctBroadExpansionObservations)
		{
			std::array<BufferedPixelSample,3> frames;
			OutwardPictureConfirmationState proof;
			for (size_t index=0;index<frames.size();++index)
			{
				const auto& frame=frames[index];
				Assert::IsTrue(frame.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
				Assert::AreEqual(68,frame.evidence.trustedBounds.top);
				Assert::AreEqual(2092,frame.evidence.trustedBounds.bottom);
				Assert::IsTrue(frame.retention.expansionStripsAvailable);
				Assert::IsFalse(frame.retention.globalNearBlack);
				const auto result=ConfirmOutwardPictureTransition(proof,BufferedScope(),frame.evidence.trustedBounds,frame.retention,11,100+index);
				Assert::IsTrue(result.broadOpposingPicture);
				Assert::AreEqual(index==2,result.authoritative);
				proof=result.state;
			}
		}
		TEST_METHOD(TwoBufferedFutureFramesPublishOnFirstChangedFrameWithoutLiveWait)
		{
			std::array<BufferedPixelSample,3> frames;
			ActivePictureDecisionTimeline timeline;
			ActivePictureTransitionModel live;
			EstablishBufferedScope(timeline,live);
			for (uint64_t sequence=100;sequence<=102;++sequence)
				Assert::IsTrue(timeline.TrackAcceptedFrame(BufferedIdentity(sequence)));
			ActivePictureFrameDecision queued;
			for (size_t index=0;index<frames.size();++index)
			{
				const auto observation=MakeActivePictureObservation(frames[index].evidence,100+index,24);
				Assert::IsTrue(timeline.TrackLookaheadEvidence(BufferedIdentity(100+index),observation,true,false));
				// Match the 24 Hz preview detector cadence: middle frame retains
				// pixel evidence but is not a second model observation.
				if (index!=1)
					Assert::AreEqual(index==2,timeline.SubmitScheduledObservation(BufferedIdentity(100+index),observation,2,2,queued));
			}
			Assert::AreEqual(uint64_t{100},queued.effectiveIdentity.acceptedSequence);
			Assert::IsTrue(timeline.IsDecisionCurrent(queued));
			Assert::IsTrue(ValidateActivePictureScheduledDecision(queued,BufferedIdentity(100),frames[0].evidence.trustedBounds,frames[0].evidence.classification)==ActivePictureScheduledDecisionValidation::ACCEPTED);
			const auto current=BufferedLiveInput(frames[0]);
			const auto admission=EvaluateTransitionAdmission(current);
			Assert::IsTrue(admission.deferOutward); // Ordinary live path still waits.
			const auto samples=BufferedSamples(frames);
			const auto proof=BuildBufferedPictureExpansion(samples.data(),samples.size(),BufferedScope(),2,2,
				queued.continuityGeneration,queued.lookaheadPolicyGeneration);
			Assert::IsTrue(proof.valid && timeline.IsDecisionCurrent(proof.decision));
			const bool buffered=ValidateBufferedPictureExpansion(proof,BufferedIdentity(100),current,
				live.WouldAdmitGeometryChange(MakeActivePictureObservation(current.evidence,100,24)));
			const bool apply=buffered && live.AdoptPublishedDecision(proof.decision.transition,
				current.evidence.classification,false,nullptr,&admission.observation.axisEvidence,true);
			Assert::IsTrue(apply,L"Buffered current plus two future frames must publish before displaying the first changed frame.");
			Assert::AreEqual(uint64_t{100},proof.decision.effectiveIdentity.acceptedSequence);
			Assert::AreEqual(uint64_t{102},proof.decision.observationIdentity.acceptedSequence);
			Assert::AreEqual(uint8_t{3},proof.decision.proofFrameCount);
			const auto measured=ResolveActivePictureRetentionHandoff(frames[0].Source(),BufferedScope(),
				current.retention,proof.decision.transition.bounds);
			Assert::IsTrue(measured.refreshed && measured.evidence.CanRetainPresentation());
			Input crop;
			crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
			crop.classification=crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.geometry=proof.decision.transition.bounds;
			crop.rasterWidth=3840; crop.rasterHeight=2160;
			crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
			crop.frameSourceSequence=100; crop.framePresentationEpoch=13;
			crop.frameLocalPresentationRetentionEvaluated=true;
			crop.frameLocalPresentationRetentionSafe=measured.evidence.CanRetainPresentation();
			const auto shown=AdmitCropPresentation({},crop,Evaluate(crop),13);
			Assert::IsFalse(shown.blocked);
			Assert::IsTrue(shown.presentation.applyCrop);
			Assert::AreEqual(68,shown.presentation.sourceBounds.top);
			Assert::AreEqual(2092,shown.presentation.sourceBounds.bottom);
		}
		TEST_METHOD(BufferedProofRequiresEverySampleToBeFreshBroadAndConsistent)
		{
			const std::array<BufferedPixelSample,3> frames;
			const auto original=BufferedSamples(frames);
			for (size_t index=0;index<3;++index)
			for (int fault=0;fault<27;++fault)
			{
				auto samples=original;
				auto& sample=samples[index];
				switch (fault)
				{
				case 0: sample.observation.available=false; break;
				case 1: sample.observation.classification=ActivePictureClassification::PROVISIONAL; break;
				case 2: sample.observation.classification=ActivePictureClassification::UNAVAILABLE; break;
				case 3: sample.observation.transitionDeferred=true; break;
				case 4: sample.observation.axisEvidence.horizontal.barCandidate=true;
					sample.observation.axisEvidence.horizontal.state=ActivePictureAxisState::UNKNOWN; break;
				case 5: sample.nearBlackEvaluated=false; break;
				case 6: sample.retention.globalNearBlack=true; break;
				case 7: sample.retention.analysisValid=false; break;
				case 8: sample.retention.presentationValid=false; break;
				case 9: sample.retention.expansionStripsAvailable=false; break;
				case 10: sample.retention.expansionBase.top+=4; break;
				case 11: sample.retention.expansionCandidate.top+=4; break;
				case 12: --sample.identity.acceptedSequence; --sample.observation.frameNumber; break;
				case 13: ++sample.identity.acceptedSequence; ++sample.observation.frameNumber; break;
				case 14: ++sample.observation.frameNumber; break;
				case 15: ++sample.identity.transportGeneration; break;
				case 16: ++sample.identity.sourceFormatGeneration; break;
				case 17: ++sample.identity.viewportGeneration; break;
				case 18: ++sample.identity.rendererGeneration; break;
				case 19: sample.identity.sourceFrameNumber=index==0 ? 101 : 100; break;
				case 20: sample.identity.captureTimestamp=(index==0 ? 101 : 100)*417083; break;
				case 21: sample.retention.expandingTop.blackFraction=1;
					sample.retention.expandingTop.continuity=1; sample.retention.expandingTop.lumaP90=64; break;
				case 22: sample.retention.expandingBottom.blackFraction=1;
					sample.retention.expandingBottom.continuity=1; sample.retention.expandingBottom.lumaP90=64; break;
				case 23: sample.observation.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
					sample.retention.expansionCandidate=sample.observation.bounds; break;
				case 24: sample.observation.bounds.bottom=BufferedScope().bottom;
					sample.retention.expansionCandidate=sample.observation.bounds; break;
				case 25: sample.observation.bounds.left=4;
					sample.retention.expansionCandidate=sample.observation.bounds; break;
				case 26: ++sample.observation.bounds.top;
					sample.retention.expansionCandidate=sample.observation.bounds; break;
				}
				const auto message=L"sample="+std::to_wstring(index)+L" fault="+std::to_wstring(fault);
				Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),samples.size(),BufferedScope(),2,2,19,23).valid,message.c_str());
			}
		}

		TEST_METHOD(ZeroOrOneLookaheadRetainsConservativeLiveConfirmation)
		{
			const std::array<BufferedPixelSample,3> frames;
			const auto samples=BufferedSamples(frames);
			for (uint8_t depth : {uint8_t{0},uint8_t{1}})
			{
				Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),depth,2,19,23).valid);
				Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,depth,19,23).valid);
				ActivePictureDecisionTimeline timeline;
				ActivePictureTransitionModel live;
				EstablishBufferedScope(timeline,live);
				auto current=BufferedLiveInput(frames[0]);
				for (uint64_t sequence=100;sequence<=103;++sequence)
				{
					current.sourceSequence=sequence;
					const auto admission=EvaluateTransitionAdmission(current);
					current.previousOutward=admission.outward.state;
					const auto transition=live.Observe(admission.observation);
					Assert::AreEqual(sequence==103,transition.publish);
					if (sequence<102) Assert::IsTrue(admission.deferOutward);
				}
			}
			for (size_t count : {size_t{0},size_t{1},size_t{2}})
				Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),count,BufferedScope(),2,2,19,23).valid);
			Assert::IsFalse(BuildBufferedPictureExpansion(nullptr,3,BufferedScope(),2,2,19,23).valid);
			Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,0,23).valid);
			Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,0).valid);
		}

		TEST_METHOD(BufferedNoiseUsesFirstFrameCoordinatesAndCannotWalkBeyondOneStep)
		{
			const std::array<BufferedPixelSample,3> frames;
			const auto original=BufferedSamples(frames);
			for (int direction : {-1,1})
			for (bool bothEdges : {false,true})
			{
				auto samples=original;
				for (size_t index : {size_t{1},size_t{2}})
				{
					samples[index].observation.bounds.top+=4*direction;
					if (bothEdges) samples[index].observation.bounds.bottom-=4*direction;
					samples[index].observation.bounds.aspectRatio=3840.0/(samples[index].observation.bounds.bottom-samples[index].observation.bounds.top);
					samples[index].retention.expansionCandidate=samples[index].observation.bounds;
				}
				const auto proof=BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,23);
				Assert::IsTrue(proof.valid);
				Assert::AreEqual(68,proof.decision.transition.bounds.top);
				Assert::AreEqual(2092,proof.decision.transition.bounds.bottom);
				Assert::IsTrue(ValidateBufferedPictureExpansion(proof,BufferedIdentity(100),BufferedLiveInput(frames[0]),true));
				// Walking by another step cannot borrow the first step's tolerance.
				samples[2].observation.bounds.top+=4*direction;
				samples[2].retention.expansionCandidate=samples[2].observation.bounds;
				Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,23).valid);
			}
		}

		TEST_METHOD(BufferedConsumerPreservesCurrentEvidenceContextAndSubtitleVetoes)
		{
			const std::array<BufferedPixelSample,3> frames;
			const auto samples=BufferedSamples(frames);
			const auto original=BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,23);
			Assert::IsTrue(original.valid);
			for (int fault=0;fault<39;++fault)
			{
				auto proof=original;
				auto current=BufferedLiveInput(frames[0]);
				auto identity=BufferedIdentity(100);
				bool eligible=true;
				switch (fault)
				{
				case 0: ++current.sourceGeneration; break;
				case 1: ++current.sourceGeneration; ++current.trustedGeneration; break;
				case 2: ++current.sourceSequence; break;
				case 3: ++identity.acceptedSequence; break;
				case 4: ++identity.captureTimestamp; break;
				case 5: current.evidence.classification=ActivePictureClassification::PROVISIONAL; break;
				case 6: current.evidence.trustedBounds.top+=4; break;
				case 7: current.retention.expansionCandidate.top+=4; break;
				case 8: current.retention.expansionBase.top+=4; break;
				case 9: current.retention.globalNearBlack=true; break;
				case 10: current.retention.expansionStripsAvailable=false; break;
				case 11: current.evidence.available=false; break;
				case 12: current.compatiblePresentation=false; break;
				case 13: current.trustedGeometryAvailable=false; break;
				case 14: current.trustedGeometry.top+=4; break;
				case 15: current.presentationBeforeObservation.top+=4; break;
				case 16: current.presentation.action=VerticalBarPresentationAction::FIT; break;
				case 17: current.presentation.action=VerticalBarPresentationAction::TRANSLATE; break;
				case 18: current.translationDriftActive=true; break;
				case 19: current.previousOutward.verticalPresentationSeen=true; break;
				case 20: eligible=false; break;
				case 21: current.evidence.axisEvidence.horizontal.barCandidate=true;
					current.evidence.axisEvidence.horizontal.state=ActivePictureAxisState::UNKNOWN; break;
				case 22: proof.valid=false; break;
				case 23: --proof.decision.proofFrameCount; break;
				case 24: proof.decision.association=ActivePictureDecisionAssociation::CONFIRMATION; break;
				case 25: --proof.decision.observationIdentity.acceptedSequence; break;
				case 26: proof.decision.configuredLookahead=1; break;
				case 27: proof.decision.effectiveLookahead=1; break;
				case 28: proof.decision.availableLookahead=1; break;
				case 29: proof.decision.transition.authoritativeClassification=ActivePictureClassification::FULL_RASTER_TRUSTED; break;
				case 30: proof.decision.transition.publish=false; break;
				case 31: proof.decision.transition.stable=false; break;
				case 32: proof.decision.continuityGeneration=0; break;
				case 33: proof.decision.lookaheadPolicyGeneration=0; break;
				case 34: proof.decision.transition.bounds.top+=4; break;
				case 35: ++identity.viewportGeneration; break;
				case 36: ++identity.rendererGeneration; break;
				case 37: ++identity.sourceFormatGeneration; break;
				case 38: ++identity.sourceFrameNumber; break;
				}
				const auto message=L"consumer fault="+std::to_wstring(fault);
				Assert::IsFalse(ValidateBufferedPictureExpansion(proof,identity,current,eligible),message.c_str());
			}
		}

		TEST_METHOD(BufferedDecisionCannotSurvivePolicyChangeOrDiscardedProofFrame)
		{
			const std::array<BufferedPixelSample,3> frames;
			const auto samples=BufferedSamples(frames);
			for (int invalidation=0;invalidation<3;++invalidation)
			{
				ActivePictureDecisionTimeline timeline;
				ActivePictureTransitionModel live;
				EstablishBufferedScope(timeline,live);
				for (uint64_t sequence=100;sequence<=102;++sequence)
					Assert::IsTrue(timeline.TrackAcceptedFrame(BufferedIdentity(sequence)));
				const auto proof=BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,
					timeline.ContinuityGeneration(),timeline.LookaheadPolicyGeneration());
				Assert::IsTrue(proof.valid && timeline.IsDecisionCurrent(proof.decision));
				if (invalidation==0) timeline.InvalidateLookaheadPolicy();
				if (invalidation==1) timeline.MarkDiscarded(BufferedIdentity(101),101);
				if (invalidation==2) timeline.Reset(8);
				Assert::IsFalse(timeline.IsDecisionCurrent(proof.decision));
			}
		}

		TEST_METHOD(RepeatedBufferedRoundTripsKeepFirstFramePixelSafeAcrossBothNoiseOrders)
		{
			for (bool noisyFirst : {false,true})
			{
				ActivePictureDecisionTimeline timeline;
				ActivePictureTransitionModel live;
				EstablishBufferedScope(timeline,live);
				for (uint64_t first : {uint64_t{100},uint64_t{200}})
				{
					const int firstTop=noisyFirst ? 72 : 68;
					const int laterTop=noisyFirst ? 68 : 72;
					const std::array<BufferedPixelSample,3> frames={BufferedPixelSample(firstTop,2160-firstTop),
						BufferedPixelSample(laterTop,2160-laterTop),BufferedPixelSample(firstTop,2160-firstTop)};
					const auto samples=BufferedSamples(frames,first);
					const auto proof=BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,23);
					Assert::IsTrue(proof.valid);
					auto current=BufferedLiveInput(frames[0]); current.sourceSequence=first;
					const bool eligible=live.WouldAdmitGeometryChange(MakeActivePictureObservation(current.evidence,first,24));
					Assert::IsTrue(ValidateBufferedPictureExpansion(proof,BufferedIdentity(first),current,eligible));
					Assert::IsTrue(live.AdoptPublishedDecision(proof.decision.transition,current.evidence.classification,false,nullptr,
						&samples[0].observation.axisEvidence,true));
					Assert::AreEqual(firstTop,proof.decision.transition.bounds.top);
					const auto measured=ResolveActivePictureRetentionHandoff(frames[0].Source(),BufferedScope(),current.retention,proof.decision.transition.bounds);
					Assert::IsTrue(measured.refreshed && measured.evidence.CanRetainPresentation());
					// Future evidence did not get installed as live candidate state.
					const auto stable=live.Observe(MakeActivePictureObservation(frames[0].evidence,first+1,24));
					Assert::IsFalse(stable.publish);
					bool returned=false;
					for (uint64_t sequence=first+10;sequence<first+14;++sequence)
						returned=live.Observe(BufferedObservation(sequence,BufferedScope())).publish || returned;
					Assert::IsTrue(returned);
				}
			}
		}
		TEST_METHOD(BufferedPublicationStaysStableThroughFollowingCleanBarMeasurementJitter)
		{
			// Keep actual pixels fixed at72. Detector coordinates alone alternate
			// between68 and72, so every disputed row remains genuine black.
			for (bool injectMovement : {false,true})
            for (bool noisyFirst : {false,true})
			{
				const std::array<BufferedPixelSample,3> frames={BufferedPixelSample(72,2088),
					BufferedPixelSample(72,2088),BufferedPixelSample(72,2088)};
				auto samples=BufferedSamples(frames);
				const int firstTop=noisyFirst ? 72 : 68;
				for (size_t index=0;index<samples.size();++index)
				{
					const int top=index==1 ? 140-firstTop : firstTop;
					samples[index].observation.bounds.top=top;
					samples[index].observation.bounds.bottom=2160-top;
					samples[index].observation.bounds.aspectRatio=3840.0/(2160-2*top);
					samples[index].retention.expansionCandidate=samples[index].observation.bounds;
				}
				const auto proof=BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,23);
				Assert::IsTrue(proof.valid);
				ActivePictureDecisionTimeline timeline;
				ActivePictureTransitionModel live;
				EstablishBufferedScope(timeline,live);
				MovingPictureTransitionState moving;
				if (injectMovement)
				{
					BufferedMotionSequence prefix; prefix.sequence=40;
					for (int top=268;top>=232;top-=4)
					{
						BufferedPixelSample movingPixels(top,2160-top); prefix.Step(movingPixels);
					}
					Assert::IsTrue(prefix.moving.active);
					BufferedPixelSample scope(276,1884);
					for (int repeat=0;repeat<4;++repeat) prefix.Step(scope);
					Assert::IsFalse(prefix.moving.active);
					live=prefix.model; moving=prefix.moving;
				}
				ActivePictureBounds geometry=BufferedScope();
				CropPresentationAdmissionState admitted;
				PresentationRecoveryState recovery;
				VerticalInspectionBridgeState inspection;
				OutwardPictureConfirmationState outward;
				for (uint64_t sequence=100;sequence<108;++sequence)
				{
					const int top=(sequence%2)==0 ? firstTop : 140-firstTop;
					auto observed=BufferedTaller(); observed.top=top; observed.bottom=2160-top;
					observed.aspectRatio=3840.0/(observed.bottom-observed.top);
					const auto measuredBase=geometry;
					auto current=BufferedLiveInput(frames[0]);
					current.sourceSequence=sequence;
					current.evidence.trustedBounds=current.evidence.proposedBounds=current.outwardCandidate=observed;
					current.trustedGeometry=current.presentationBeforeObservation=geometry;
					current.retention=EvaluateActivePicturePresentationRetention(frames[0].Source(),geometry);
					current.retention.expansionCandidate=observed; // Synthetic detector-step error only.
					current.previousOutward=outward;
					moving=ObserveMovingPictureTransition(moving,BufferedIdentity(sequence),current);
                    Assert::IsFalse(moving.active,L"Prior authored movement must not reclassify clean-bar jitter.");
                    auto admission=EvaluateTransitionAdmission(current);
                    ConstrainMovingPictureTransition(moving,admission);
					outward=admission.outward.state;
					if (sequence==100)
					{
						Assert::IsTrue(ValidateBufferedPictureExpansion(proof,BufferedIdentity(sequence),current,
							live.WouldAdmitGeometryChange(MakeActivePictureObservation(current.evidence,sequence,24))));
						Assert::IsTrue(live.AdoptPublishedDecision(proof.decision.transition,current.evidence.classification,
							false,nullptr,&admission.observation.axisEvidence,true));
						geometry=proof.decision.transition.bounds;
					}
					else Assert::IsFalse(live.Observe(admission.observation).publish);
					const auto pixels=ResolveActivePictureRetentionHandoff(frames[0].Source(),measuredBase,current.retention,geometry);
					Assert::IsTrue(pixels.evidence.excludedBandsPixelSafe);
					Input crop;
					crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
					crop.classification=crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
					crop.geometry=geometry; crop.rasterWidth=3840; crop.rasterHeight=2160;
					crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
					crop.frameSourceSequence=sequence; crop.framePresentationEpoch=13;
					crop.latestObservationSupportsCrop=(geometry.top<=observed.top && geometry.bottom>=observed.bottom) ||
						IsPixelSafeCropReaffirmation(geometry,observed,pixels.evidence.excludedBandsPixelSafe);
					crop.barCropRefinementPending=!crop.latestObservationSupportsCrop;
					crop.frameLocalPresentationRetentionEvaluated=true;
					crop.frameLocalPresentationRetentionSafe=pixels.evidence.CanRetainPresentation();
					auto candidate=Evaluate(crop);
					VerticalInspectionBridgeInput bridge;
					bridge.previous=inspection; bridge.candidate=!crop.latestObservationSupportsCrop;
					bridge.retentionRequested=!candidate.applyCrop && candidate.withdrawalCause==WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED;
					bridge.cropAuthorityResolved=crop.latestObservationSupportsCrop;
					bridge.sourceGeneration=7; bridge.sourceSequence=sequence; bridge.presentationEpoch=13;
					bridge.trustedBase=geometry;
					inspection=UpdateVerticalInspectionBridge(bridge).state;
					Assert::IsFalse(inspection.failOpenLatched);
					PresentationRecoveryInput recover;
					recover.previous=recovery; recover.crop=crop; recover.candidate=candidate;
					recover.measurementCurrent=recover.retentionEvaluated=recover.nearBlackEvaluated=true;
					recover.retentionBounds=pixels.bounds; recover.retentionSourceGeneration=7; recover.retentionSourceSequence=sequence;
					recover.observationAvailable=pixels.evidence.proposedBoundsAvailable;
					recover.observation=pixels.evidence.activePicture.proposedBounds;
					recover.observedTrustedCrop=pixels.evidence.activePicture.trustedBounds;
					recover.observationClassification=pixels.evidence.activePicture.classification;
					recover.excludedBandsPixelSafe=pixels.evidence.excludedBandsPixelSafe;
					recover.presentationEpoch=13; recover.currentTick=sequence*42; recover.framesPerSecond=24;
					const auto recovered=EvaluatePresentationRecovery(recover); recovery=recovered.state;
					const auto visible=AdmitCropPresentation(admitted,crop,recovered.presentation,13); admitted=visible.state;
					const auto diagnostic=L"following sequence="+std::to_wstring(sequence)+L" first top="+std::to_wstring(firstTop);
					Assert::IsFalse(recovery.active,diagnostic.c_str());
					Assert::IsTrue(visible.presentation.applyCrop,diagnostic.c_str());
					Assert::AreEqual(firstTop,visible.presentation.sourceBounds.top,diagnostic.c_str());
					Assert::AreEqual(2160-firstTop,visible.presentation.sourceBounds.bottom,diagnostic.c_str());
				}
			}
		}

		TEST_METHOD(ActualFourPixelPictureGrowthIsNotMislabelledCleanBarJitter)
		{
			// This is an actual new bright strip, unlike the preceding test's
			// unchanged black pixels. The current-pixel safety veto must survive.
			const std::array<BufferedPixelSample,3> frames={BufferedPixelSample(72,2088),
				BufferedPixelSample(68,2092),BufferedPixelSample(72,2088)};
			const auto samples=BufferedSamples(frames);
			const auto proof=BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,23);
			Assert::IsTrue(proof.valid);
			const auto first=EvaluateActivePicturePresentationRetention(frames[0].Source(),proof.decision.transition.bounds);
			const auto next=EvaluateActivePicturePresentationRetention(frames[1].Source(),proof.decision.transition.bounds);
			Assert::IsTrue(first.excludedBandsPixelSafe);
			Assert::IsFalse(next.excludedBandsPixelSafe);
			Assert::IsFalse(IsPixelSafeCropReaffirmation(proof.decision.transition.bounds,
				frames[1].evidence.trustedBounds,next.excludedBandsPixelSafe));
			Assert::IsTrue(next.outwardVisibleBoundsAvailable);
		}

		TEST_METHOD(SourceCounterGapCannotHideBehindContinuousAcceptedSequences)
		{
			const std::array<BufferedPixelSample,3> frames;
			const auto original=BufferedSamples(frames);
			for (size_t index : {size_t{1},size_t{2}})
			{
				auto samples=original;
				++samples[index].identity.sourceFrameNumber;
				Assert::IsFalse(BuildBufferedPictureExpansion(samples.data(),3,BufferedScope(),2,2,19,23).valid);
			}
		}
	};
}
