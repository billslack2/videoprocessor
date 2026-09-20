#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureTransitionModel.h>
#include <vprenderer/AlphaSourceCropPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
	namespace
	{
		ActivePictureBounds HandoffScope()
		{
			return {0,276,3840,1884,3840,2160,3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
		}

		ActivePictureBounds HandoffTaller()
		{
			return {0,68,3840,2092,3840,2160,3840.0/2024.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
		}

		struct HandoffSequence
		{
			ActivePictureTransitionModel model;
			TransitionAdmissionInput input;
			TransitionAdmissionDecision admission;
			ActivePictureTransitionDecision transition;
			PictureTransitionHandoff handoff;

			HandoffSequence()
			{
				for (uint64_t seq=1;seq<=4;++seq)
					model.Observe({HandoffScope(),seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
				input.trustedGeometry=input.presentationBeforeObservation=HandoffScope();
				input.trustedGeometryAvailable=input.compatiblePresentation=input.evidence.available=true;
				input.trustedGeneration=input.sourceGeneration=input.presentationEvidenceGeneration=7;
				input.sourceSequence=100; input.framesPerSecond=24;
				SetTarget(HandoffTaller());
				input.retention.analysisValid=input.retention.presentationValid=true;
				input.retention.expansionStripsAvailable=true;
				input.retention.expansionBase=HandoffScope();
				auto& edge=input.retention.expandingTop;
				edge.barPixels=206; edge.blackFraction=.2; edge.continuity=.4; edge.lumaP90=300;
				input.retention.expandingBottom=edge;
			}

			void SetTarget(const ActivePictureBounds& target)
			{
				input.evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
				input.evidence.trustedBounds=input.evidence.proposedBounds=input.outwardCandidate=target;
				input.retention.expansionCandidate=target;
			}

			void Step()
			{
				const bool eligible=model.WouldAdmitGeometryChange(MakeActivePictureObservation(
					input.evidence,input.sourceSequence,input.framesPerSecond));
				admission=EvaluateTransitionAdmission(input);
				transition=model.Observe(admission.observation);
				handoff=MakePictureTransitionHandoff(input,admission,transition,eligible,9);
				input.previousOutward=admission.outward.state;
			}

			Input Crop() const
			{
				Input crop;
				crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
				crop.classification=crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.geometry=HandoffScope();
				crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
				crop.frameSourceSequence=input.sourceSequence; crop.framePresentationEpoch=9;
				crop.rasterWidth=3840; crop.rasterHeight=2160;
				crop.barCropRefinementPending=true;
				crop.frameLocalPresentationRetentionEvaluated=true;
				crop.pictureTransitionHandoff=handoff;
				return crop;
			}
		};
	}

	TEST_CLASS(PictureTransitionHandoffTests)
	{
	public:
		TEST_METHOD(CurrentBroadProofRetainsOnlyUntilRealPublication)
		{
			HandoffSequence sequence;
			for (uint64_t seq=100;seq<=103;++seq)
			{
				sequence.input.sourceSequence=seq; sequence.Step();
				Assert::AreEqual(seq<103,sequence.handoff.active);
				Assert::AreEqual(seq==103,sequence.transition.publish);
				if (seq<103)
				{
					const auto crop=sequence.Crop();
					Assert::IsTrue(HasCurrentPictureTransitionHandoff(crop));
					const auto shown=Evaluate(crop);
					Assert::IsTrue(shown.owner==DecisionOwner::PICTURE_CONFIRMATION);
					Assert::AreEqual(276,shown.sourceBounds.top);
					Assert::AreEqual(1884,shown.sourceBounds.bottom);
				}
			}
		}

		TEST_METHOD(HandoffCreationRejectsUnprovenOrMismatchedEvidence)
		{
			for (int fault=0;fault<20;++fault)
			{
				HandoffSequence sequence;
				auto& input=sequence.input;
				switch (fault)
				{
				case 0: input.evidence.available=false; break;
				case 1: input.evidence.classification=ActivePictureClassification::PROVISIONAL; break;
				case 2: input.retention.globalNearBlack=true; break;
				case 3: input.retention.analysisValid=false; break;
				case 4: input.retention.presentationValid=false; break;
				case 5: input.retention.expansionStripsAvailable=false; break;
				case 6: input.retention.expansionBase.top+=2; break;
				case 7: input.retention.expansionCandidate.top+=2; break;
				case 8: input.trustedGeneration=6; break;
				case 9: input.sourceGeneration=0; break;
				case 10: input.sourceSequence=0; break;
				case 11: input.presentationBeforeObservation.top+=2; break;
				case 12: input.outwardCandidate.top+=2; break;
				case 13: input.trustedGeometry.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH; break;
				case 14: { auto target=HandoffTaller(); target.left=2;
					target.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH; sequence.SetTarget(target); break; }
				case 15: { auto target=HandoffTaller(); target.bottom=HandoffScope().bottom; sequence.SetTarget(target); break; }
				case 16: { auto target=HandoffTaller(); ++target.top; sequence.SetTarget(target); break; }
				case 17: input.retention.expandingBottom.blackFraction=1;
					input.retention.expandingBottom.continuity=1; input.retention.expandingBottom.lumaP90=64; break;
				case 18: input.trustedGeometryAvailable=false; break;
				case 19: input.compatiblePresentation=false; break;
				}
				sequence.Step();
				Assert::IsFalse(sequence.handoff.active);
			}
		}

		TEST_METHOD(CurrentCertificateCannotCrossPresentationContextOrOverrideSafety)
		{
			HandoffSequence sequence; sequence.Step();
			Assert::IsTrue(sequence.handoff.active);
			for (int fault=0;fault<19;++fault)
			{
				auto crop=sequence.Crop();
				switch (fault)
				{
				case 0: ++crop.frameSourceSequence; break;
				case 1: ++crop.frameSourceGeneration; break;
				case 2: ++crop.framePresentationEpoch; break;
				case 3: crop.geometry.top+=2; break;
				case 4: crop.geometry.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH; break;
				case 5: crop.rasterWidth=1920; break;
				case 6: crop.geometrySourceGeneration=6; break;
				case 7: crop.latestObservationIsProvisional=true; break;
				case 8: crop.latestObservationIsUnavailable=true; break;
				case 9: crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL; break;
				case 10: crop.nearBlackEpisodeRetainCrop=true; break;
				case 11: crop.nearBlackEpisodeFullRaster=true; break;
				case 12: crop.fullRasterPresentationAuthoritative=true; break;
				case 13: crop.presentationFailOpen=true; break;
				case 14: crop.barCropRefinementHorizontalConflict=true; break;
				case 15: crop.sharedGeometryAvailable=false; break;
				case 16: crop.automaticCropEnabled=false; break;
				case 17: crop.classification=ActivePictureClassification::UNAVAILABLE; break;
				case 18: crop.pictureTransitionHandoff.active=false; break;
				}
				Assert::IsFalse(HasCurrentPictureTransitionHandoff(crop));
				Assert::IsTrue(Evaluate(crop).owner!=DecisionOwner::PICTURE_CONFIRMATION);
			}
		}

		TEST_METHOD(HandoffCannotAcquireNeverPresentedCropOrBypassRecovery)
		{
			HandoffSequence sequence; sequence.Step();
			auto crop=sequence.Crop();
			const auto candidate=Evaluate(crop);
			Assert::IsTrue(candidate.applyCrop);
			const auto unseen=AdmitCropPresentation({},crop,candidate,9);
			Assert::IsTrue(unseen.blocked);
			Assert::IsFalse(unseen.presentation.applyCrop);
			auto acquisition=crop; acquisition.pictureTransitionHandoff={};
			acquisition.latestObservationSupportsCrop=true;
			const auto admitted=AdmitCropPresentation({},acquisition,Evaluate(acquisition),9);
			Assert::IsTrue(AdmitCropPresentation(admitted.state,crop,candidate,9).presentation.applyCrop);
			PresentationRecoveryInput recovery;
			recovery.previous.active=true; recovery.previous.trustedCrop=crop.geometry;
			recovery.previous.sourceGeneration=7; recovery.previous.presentationEpoch=9;
			recovery.crop=crop; recovery.candidate=candidate; recovery.presentationEpoch=9;
			recovery.measurementCurrent=recovery.retentionEvaluated=recovery.nearBlackEvaluated=true;
			recovery.retentionBounds=crop.geometry; recovery.retentionSourceGeneration=7;
			recovery.retentionSourceSequence=crop.frameSourceSequence;
			recovery.observationAvailable=true;
			recovery.observation=recovery.observedTrustedCrop=HandoffTaller();
			recovery.observationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
			const auto result=EvaluatePresentationRecovery(recovery);
			Assert::IsTrue(result.state.active);
			Assert::IsFalse(result.presentation.applyCrop);
		}

		TEST_METHOD(GeometryDeadbandLeavesOrdinaryBoundedFitEligible)
		{
			HandoffSequence sequence;
			auto minorCandidate=HandoffScope(); minorCandidate.top-=8; minorCandidate.bottom+=8;
			minorCandidate.aspectRatio=3840.0/(minorCandidate.bottom-minorCandidate.top); sequence.SetTarget(minorCandidate);
			VerticalFitConfirmationState fitState;
			for (uint64_t seq=100;seq<108;++seq)
			{
				sequence.input.sourceSequence=seq; sequence.Step();
				Assert::IsFalse(sequence.handoff.active);
				Assert::IsFalse(sequence.transition.publish);
				VerticalBarContentInput bar;
				bar.upperContent=bar.lowerContent=true;
				bar.upperOccupiedDepth=bar.lowerOccupiedDepth=180;
				bar.upperPeakSamples=bar.lowerPeakSamples=1500;
				bar.upperBarPixels=bar.lowerBarPixels=276; bar.sampledColumns=1800;
				bar.upperRequiredShift=bar.lowerRequiredShift=32;
				const auto fit=ConfirmVerticalFit(fitState,EvaluateVerticalBarContent(bar),seq);
				fitState=fit.state;
				if (fit.effective.action==VerticalBarPresentationAction::FIT)
				{
					auto crop=sequence.Crop();
					PresentationEnvelopeGeometryInput envelope;
					envelope.trustedPicture=crop.geometry; envelope.observedContent=minorCandidate;
					envelope.observedContentAvailable=envelope.expandTop=envelope.expandBottom=true;
					envelope.verticalPadding=24;
					const auto bounds=BuildPresentationEnvelope(envelope);
					crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
					crop.outwardExpansion=bounds.bounds; crop.outwardExpansionSourceGeneration=7;
					const auto shown=Evaluate(crop);
					Assert::IsTrue(shown.applyCrop && shown.outwardExpanded);
					Assert::AreEqual(244,shown.sourceBounds.top);
					Assert::AreEqual(1916,shown.sourceBounds.bottom);
				}
				else Assert::AreEqual(uint64_t{100},seq);
			}
		}

		TEST_METHOD(TranslatedAndDriftingEpisodesRemainExcludedAcrossTheirProof)
		{
			for (bool drift : {false,true})
			{
				HandoffSequence sequence;
				sequence.input.translationDriftActive=drift;
				if (!drift) sequence.input.presentation.action=VerticalBarPresentationAction::TRANSLATE;
				sequence.Step();
				Assert::IsTrue(sequence.admission.outward.state.verticalPresentationSeen);
				Assert::IsFalse(sequence.handoff.active);
				sequence.input.translationDriftActive=false;
				sequence.input.presentation.action=VerticalBarPresentationAction::FIT;
				for (uint64_t seq=101;seq<=102;++seq)
				{
					sequence.input.sourceSequence=seq; sequence.Step();
					Assert::IsTrue(sequence.admission.outward.state.verticalPresentationSeen);
					Assert::IsFalse(sequence.handoff.active);
				}
			}
		}

		TEST_METHOD(PreexistingFitCannotSnapBackToBaseAtHandoffStart)
		{
			HandoffSequence sequence;
			sequence.input.presentation.action=VerticalBarPresentationAction::FIT;
			sequence.Step();
			Assert::IsFalse(sequence.handoff.active,
				L"An already visible FIT must not be replaced by the old base before picture publication.");
		}

		TEST_METHOD(FitCreatedInsidePictureProofDoesNotStealPresentation)
		{
			HandoffSequence sequence; sequence.Step();
			Assert::IsTrue(sequence.handoff.active);
			sequence.input.presentation.action=VerticalBarPresentationAction::FIT;
			sequence.input.sourceSequence=101; sequence.Step();
			Assert::IsTrue(sequence.handoff.active);
			Assert::IsFalse(sequence.admission.outward.state.verticalPresentationSeen);
		}

		TEST_METHOD(AlternatingBoundaryCannotReservePresentationIndefinitely)
		{
			HandoffSequence sequence;
			unsigned heldAfterPublicationBudget=0, ordinaryFitAfterBudget=0;
			for (uint64_t seq=100;seq<112;++seq)
			{
				auto target=HandoffTaller();
				if ((seq&1)!=0) target.top+=8; // beyond one scan step; authority must still reset
				target.aspectRatio=3840.0/(target.bottom-target.top);
				sequence.SetTarget(target); sequence.input.sourceSequence=seq; sequence.Step();
				Assert::IsFalse(sequence.transition.publish,
					L"Changing raw boundaries must not acquire authority through a presentation timeout.");
				if (seq>=103)
				{
					if (sequence.handoff.active) ++heldAfterPublicationBudget;
					auto crop=sequence.Crop();
					PresentationEnvelopeGeometryInput envelope;
					envelope.trustedPicture=crop.geometry; envelope.observedContent=target;
					envelope.observedContentAvailable=envelope.expandTop=envelope.expandBottom=true;
					envelope.verticalPadding=48;
					const auto bounds=BuildPresentationEnvelope(envelope);
					crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
					crop.outwardExpansion=bounds.bounds; crop.outwardExpansionSourceGeneration=7;
					const auto shown=Evaluate(crop);
					if (shown.applyCrop && shown.outwardExpanded) ++ordinaryFitAfterBudget;
				}
			}
			Assert::AreEqual(0u,heldAfterPublicationBudget,
				L"Candidate jitter must not renew the short arbitration window on each source frame.");
			Assert::AreEqual(9u,ordinaryFitAfterBudget);
		}
		TEST_METHOD(OneScanStepJitterContinuesAnchoredProofAtBothRasters)
		{
			for (int scale : {1,2})
			{
				HandoffSequence sequence;
				auto base=HandoffScope(), target=HandoffTaller();
				for (auto* bounds : {&base,&target})
				{
					bounds->top/=scale; bounds->bottom/=scale; bounds->right/=scale;
					bounds->rasterWidth/=scale; bounds->rasterHeight/=scale;
					bounds->aspectRatio=double(bounds->right)/(bounds->bottom-bounds->top);
				}
				sequence.model.Reset();
				for (uint64_t seq=1;seq<=4;++seq)
					sequence.model.Observe({base,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
				sequence.input.trustedGeometry=sequence.input.presentationBeforeObservation=base;
				sequence.input.retention.expansionBase=base;
				for (uint64_t seq=100;seq<=103;++seq)
				{
					auto current=target;
					if ((seq&1)!=0) current.top+=4/scale;
					current.aspectRatio=double(current.right)/(current.bottom-current.top);
					sequence.SetTarget(current); sequence.input.sourceSequence=seq; sequence.Step();
					Assert::AreEqual(static_cast<unsigned>(seq<102 ? seq-99 : 3),
						sequence.admission.outward.state.confirmations);
					Assert::AreEqual(seq<103,sequence.handoff.active);
					Assert::AreEqual(seq==103,sequence.transition.publish);
				}
			}
		}

		TEST_METHOD(ScanStepAllowanceCannotCreepWithEachSuccessiveObservation)
		{
			HandoffSequence sequence;
			for (uint64_t seq=100;seq<=102;++seq)
			{
				auto current=HandoffTaller(); current.top+=static_cast<int>(seq-100)*4;
				current.aspectRatio=3840.0/(current.bottom-current.top);
				sequence.SetTarget(current); sequence.input.sourceSequence=seq; sequence.Step();
				Assert::IsFalse(sequence.transition.publish);
				Assert::AreEqual(seq==101 ? 2u : 1u,sequence.admission.outward.state.confirmations);
			}
		}

		TEST_METHOD(ExactCurrentStripCertificateCannotBeBorrowedWithinNoiseAllowance)
		{
			HandoffSequence sequence; sequence.Step();
			Assert::AreEqual(1u,sequence.admission.outward.state.confirmations);
			auto current=HandoffTaller(); current.top+=4;
			sequence.SetTarget(current);
			sequence.input.retention.expansionCandidate=HandoffTaller();
			sequence.input.sourceSequence=101; sequence.Step();
			Assert::IsFalse(sequence.handoff.active);
			Assert::IsFalse(sequence.admission.outward.broadOpposingPicture);
			Assert::AreEqual(0u,sequence.admission.outward.state.confirmations);
		}

		TEST_METHOD(OutwardProofCannotTransferToDifferentBaseAxesOrSource)
		{
			for (int change=0;change<3;++change)
			{
				HandoffSequence sequence; sequence.Step();
				sequence.input.sourceSequence=101; sequence.Step();
				Assert::AreEqual(2u,sequence.admission.outward.state.confirmations);
				sequence.input.sourceSequence=102;
				if (change==0)
				{
					sequence.input.trustedGeometry.top+=4;
					sequence.input.presentationBeforeObservation=sequence.input.trustedGeometry;
					sequence.input.retention.expansionBase=sequence.input.trustedGeometry;
				}
				if (change==1)
				{
					sequence.input.trustedGeometry.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
					sequence.input.presentationBeforeObservation=sequence.input.trustedGeometry;
					sequence.input.retention.expansionBase=sequence.input.trustedGeometry;
				}
				if (change==2) sequence.input.sourceGeneration=sequence.input.trustedGeneration=8;
				sequence.Step();
				Assert::AreEqual(1u,sequence.admission.outward.state.confirmations,
					L"Outward evidence from another base or source cannot finish current proof.");
				Assert::IsFalse(sequence.admission.outward.authoritative);
			}
		}

		TEST_METHOD(RepeatCannotFinishProofAndInterruptedEvidenceCannotRetainCertificate)
		{
			HandoffSequence sequence; sequence.Step();
			for (int repeat=0;repeat<5;++repeat)
			{
				sequence.Step();
				Assert::AreEqual(1u,sequence.admission.outward.state.confirmations);
				Assert::IsFalse(sequence.transition.publish);
			}
			sequence.input.sourceSequence=102; sequence.Step();
			Assert::AreEqual(1u,sequence.admission.outward.state.confirmations);
			sequence.input.sourceSequence=103; sequence.input.evidence.available=false; sequence.Step();
			Assert::IsFalse(sequence.handoff.active);
			sequence.input.sourceSequence=104; sequence.input.evidence.available=true; sequence.Step();
			Assert::AreEqual(1u,sequence.admission.outward.state.confirmations);
		}
	};
}

