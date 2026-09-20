#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>
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
	}
	TEST_CLASS(LookaheadOutwardProofTests)
	{
	public:
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
					const auto admission=EvaluateTransitionAdmission(current);
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
