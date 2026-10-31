#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/MadVRShaderRuntimeState.h>
#include <ActivePictureDecisionTimeline.h>
#include <SceneDetector.h>
#include <CropDiagnosticThrottle.h>
#include <vector>
#include <limits>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/AnamorphicPresentation.h>
#include <NlsGeometryPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <SubtitlePreviewMode.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
	namespace
	{
		Input TrustedScopeCrop()
		{
			Input input;
			input.automaticCropEnabled = true;
			input.sharedGeometryAvailable = true;
			input.latestObservationSupportsCrop = true;
			input.classification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.geometry = {
				0, 274, 3840, 1884, 3840, 2160, 2.3851, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.geometrySourceGeneration = 7;
			input.frameSourceGeneration = 7;
			input.rasterWidth = 3840;
			input.rasterHeight = 2160;
			return input;
		}

        void QualifyExplicitCaptionFitFixture(PresentationRecoveryInput& input,
            const ActivePictureBounds& independentlyConfirmedCaptionBounds)
        {
            // The caller explicitly declares an accepted current caption/backing
            // in these positive fixtures. Raw visibility and dense FIT completion
            // are deliberately insufficient in the negative fixtures.
            input.crop.framePresentationEpoch=input.presentationEpoch;
            ConfirmedFitOwnershipInput owner;
            owner.origin=ConfirmedFitOwnershipOrigin::CURRENT_CAPTION;
            owner.current=owner.confirmed=true;
            owner.nativeBase=input.crop.geometry;
            owner.approvedBounds=input.candidate.sourceBounds;
            owner.evidenceBounds=independentlyConfirmedCaptionBounds;
            owner.sourceGeneration=input.crop.frameSourceGeneration;
            owner.sourceSequence=input.crop.frameSourceSequence;
            owner.presentationEpoch=input.presentationEpoch;
            input.confirmedFitOwnership=QualifyConfirmedFitOwnership(owner);
            Assert::IsTrue(input.confirmedFitOwnership.available,
                L"This positive fixture must originate an explicit current bounded caption owner.");
        }

		CaptionBoundedFitAcquisitionInput MakeCaptionFitInput(
			Input& crop, int rasterWidth, int rasterHeight,
			int pictureTop, int pictureBottom, int captionLeft,
			int captionTop, int captionRight, int captionBottom)
		{
			crop = TrustedScopeCrop();
			crop.rasterWidth = rasterWidth;
			crop.rasterHeight = rasterHeight;
			crop.frameSourceSequence = 8224;
			crop.geometry = {
				0, pictureTop, rasterWidth, pictureBottom,
				rasterWidth, rasterHeight,
				static_cast<double>(rasterWidth) / (pictureBottom - pictureTop),
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			crop.geometrySourceGeneration = crop.frameSourceGeneration;
			crop.currentVisibleBoundsAvailable = true;
			crop.currentVisibleBase = crop.geometry;
			crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
			crop.currentVisibleSourceSequence = crop.frameSourceSequence;
			crop.currentVisibleBounds = crop.geometry;
			crop.currentVisibleBounds.trustedBarAxes =
				ActivePictureBounds::BarAxes::NONE;
			if (captionTop < pictureTop)
				crop.currentVisibleBounds.top = (captionTop - 2) & ~1;
			if (captionBottom > pictureBottom)
				crop.currentVisibleBounds.bottom = (captionBottom + 3) & ~1;
			crop.currentVisibleBounds.aspectRatio =
				static_cast<double>(rasterWidth) /
				(crop.currentVisibleBounds.bottom - crop.currentVisibleBounds.top);

			CaptionBoundedFitAcquisitionInput request;
			request.crop = &crop;
			request.captionBounds = {
				captionLeft, captionTop, captionRight, captionBottom,
				rasterWidth, rasterHeight,
				static_cast<double>(captionRight - captionLeft) /
					(captionBottom - captionTop),
				ActivePictureBounds::BarAxes::NONE };
			request.captionSourceGeneration = crop.frameSourceGeneration;
			request.captionSourceSequence = crop.frameSourceSequence;
			request.captionConfirmed = true;
			request.initialAcquisition = true;
			request.translationProposed = true;
			return request;
		}
		PresentationRecoveryInput AgencyBoundedRecovery(uint64_t sequence = 4013)
		{
			PresentationRecoveryInput input;
			input.crop = TrustedScopeCrop();
			auto& c = input.crop;
			c.geometry.top = 276;
			c.geometry.aspectRatio = 3840.0 / 1608.0;
			c.frameSourceSequence = sequence;
			c.latestObservationSupportsCrop = false;
			c.latestObservationIsProvisional = true;
			c.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
			c.presentationFailOpen = true; // Expired inspection, no dense owner.
			c.currentVisibleBoundsAvailable = true;
			c.currentVisibleBase = c.geometry;
			c.currentVisibleSourceGeneration = 7;
			c.currentVisibleSourceSequence = sequence;
			c.currentVisibleBounds = c.geometry;
			c.currentVisibleBounds.bottom = 2160;
			c.currentVisibleBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			input.measurementCurrent = input.retentionEvaluated = input.nearBlackEvaluated = true;
			input.retentionBounds = c.geometry;
			input.retentionSourceGeneration = 7;
			input.retentionSourceSequence = sequence;
			input.observationAvailable = true;
			input.observationClassification = ActivePictureClassification::PROVISIONAL;
			input.observation = c.currentVisibleBounds;
			input.framesPerSecond = 24;
			input.presentationEpoch = 9;
			input.candidate = Evaluate(c);
			return input;
		}
        PresentationRecoveryInput ToyStoryNearBlackRecovery(uint64_t sequence = 30120)
        {
            auto input = AgencyBoundedRecovery(sequence);
            auto& c = input.crop;
            c.geometry = {0,42,3840,2118,3840,2160,3840.0/2076.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            c.presentationFailOpen = false;
            c.nearBlackEpisodeFullRaster = true;
            c.latestObservationIsProvisional = false;
            c.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            input.observationClassification = c.latestObservationClassification;
            input.retentionBounds = c.currentVisibleBase = c.geometry;
            input.observation = c.geometry;
            input.observation.top = 40;
            c.currentVisibleBounds = c.geometry;
            c.currentVisibleBounds.top = 27; // Log: rows 40/41 + conservative extent margin.
            input.previousAdmission.available = true;
            input.previousAdmission.trustedCrop = c.geometry;
            input.previousAdmission.sourceGeneration = c.frameSourceGeneration;
            input.previousAdmission.presentationEpoch = input.presentationEpoch;
            input.submittedScopeAnchor.available = true;
            input.submittedScopeAnchor.crop = c.geometry;
            input.submittedScopeAnchor.sourceGeneration = c.frameSourceGeneration;
            input.submittedScopeAnchor.presentationEpoch = input.presentationEpoch;
            input.submittedScopeAnchor.lastSubmittedSourceSequence = sequence - 1;
            input.candidate = Evaluate(c);
            return input;
        }
        NearBlackPresentationEpisodeState ToyStoryRetainedEpisode(const PresentationRecoveryInput& recovery)
        {
            NearBlackPresentationEpisodeInput input;
            input.measurementCurrent = input.nearBlackEvaluated = input.globalNearBlack = true;
            input.trustedCropAvailable = true;
            input.trustedCrop = recovery.crop.geometry;
            input.sourceGeneration = recovery.crop.frameSourceGeneration;
            input.sourceSequence = recovery.crop.frameSourceSequence - 6;
            input.presentationEpoch = recovery.presentationEpoch;
            auto entry = EvaluateNearBlackPresentationEpisode(input);
            input.previous = entry.state;
            input.sourceSequence = recovery.crop.frameSourceSequence;
            input.globalNearBlack = false;
            input.boundedVisibleContentOutsideCrop = true;
            return EvaluateNearBlackPresentationEpisode(input).state;
        }
        NearBlackPresentationEpisodeState CommitNearBlackForTest(
            const NearBlackBoundedPresentationDecision& proposal,
            NearBlackPresentationEpisodeState episode,
            const CropPresentationAdmissionState& admission,
            uint64_t currentSourceSequence, bool renderedAndSubmitted = true,
            bool sceneBoundary = false)
        {
            if (!proposal.proposalAvailable && !proposal.failOpenProposal)
                return proposal.state;
            NearBlackBoundedPresentationCommitInput input;
            input.currentEpisode = episode;
            input.currentAdmission = admission;
            input.proposalMode = proposal.proposalMode;
            input.proposalBase = proposal.proposalBase;
            input.proposalBounds = proposal.proposalBounds;
            input.failOpenProposal = proposal.failOpenProposal;
            input.sourceGeneration = proposal.sourceGeneration;
            input.presentationEpoch = proposal.presentationEpoch;
            input.currentSourceSequence = currentSourceSequence;
            input.selectedSourceSequence = proposal.sourceSequence;
            input.proposalSourceSequence = proposal.sourceSequence;
            input.episodeStartSequence = proposal.episodeStartSequence;
            input.selectedActualOwner = proposal.presentation.owner;
            input.selectedActualBounds = proposal.presentation.sourceBounds;
            input.renderedAndSubmitted = renderedAndSubmitted;
            input.sceneBoundary = sceneBoundary;
            return CommitNearBlackBoundedPresentation(input).state;
        }

        PresentationRecoveryInput ExpiredFitInspection()
        {
            auto input = AgencyBoundedRecovery(4525);
            auto& crop = input.crop;
            crop.presentationFailOpen = false;
            crop.verticalInspectionPending = true;
            crop.verticalInspectionSourceGeneration = 7;
            crop.verticalInspectionSourceSequence = 4525;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            crop.currentVisibleBounds.top = 85;
            crop.currentVisibleBounds.bottom = 2105;
            input.observation.top = 188;
            input.observation.bottom = 2092;

            auto prior = crop;
            prior.frameSourceSequence = 4524;
            prior.latestObservationSupportsCrop = true;
            prior.verticalInspectionPending = false;
            prior.outwardPresentationActive = prior.outwardExpansionAvailable = true;
            prior.outwardExpansionSourceGeneration = 7;
            prior.outwardExpansion = crop.geometry;
            prior.outwardExpansion.top = 34;
            prior.outwardExpansion.bottom = 2126;
            const auto admission = AdmitCropPresentation({}, prior, Evaluate(prior), 9).state;
            input.previousAdmission = admission;
            input.candidate = Evaluate(crop);
            return input;
        }

        PresentationRecoveryInput ChangedContractBoundedRecovery()
        {
            auto input=AgencyBoundedRecovery(6534);
            input.crop.currentVisibleBounds.top=input.observation.top=54;
            input.crop.currentVisibleBounds.bottom=input.observation.bottom=2106;
            input.previous=EvaluatePresentationRecovery(input).state;
            input.previous.samples=3;
            auto& c=input.crop;
            c.geometry={0,68,3840,2092,3840,2160,3840.0/2024.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            c.frameSourceSequence=input.retentionSourceSequence=6542;
            c.currentVisibleBoundsAvailable=false;
            c.presentationFailOpen=c.latestObservationIsProvisional=false;
            c.latestObservationSupportsCrop=true;
            c.latestObservationClassification=input.observationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.retentionBounds=input.observation=input.observedTrustedCrop=c.geometry;
            input.excludedBandsPixelSafe=true;
            input.candidate=Evaluate(c);
            return input;
        }

        HeldBarAnalysisInput ExpiredTranslationWithTwoEdgeEnvelope()
        {
            HeldBarAnalysisInput input;
            input.trustedBarGeometryAvailable=input.storedBaseMatchesTrustedGeometry=input.currentEnvelopeAvailable=true;
            input.latestClassification=ActivePictureClassification::PROVISIONAL;
            input.trustedGeometry={0,276,3840,1884,3840,2160,3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            input.currentEnvelope=input.trustedGeometry;
            input.currentEnvelope.top=54; input.currentEnvelope.bottom=2106;
            input.currentEnvelope.aspectRatio=3840.0/(2106-54);
            input.presentation.action=VerticalBarPresentationAction::TRANSLATE;
            input.presentation.translationPixels=178;
            input.presentation.lastDetectionTick=1000; input.presentation.sourceSequence=50;
            input.evidenceSourceGeneration=input.currentSourceGeneration=7;
            input.currentTick=3001; input.holdMs=2000; input.currentSourceSequence=100;
            return input;
        }

        VerticalBarPresentationUpdateInput FreshFitAtTranslationExpiry()
        {
            const auto held=ExpiredTranslationWithTwoEdgeEnvelope();
            VerticalBarPresentationUpdateInput update;
            update.previous=held.presentation;
            update.current.action=VerticalBarPresentationAction::FIT;
            update.upperContent=update.lowerContent=true;
            update.upperContentTop=54; update.lowerContentBottom=2106;
            update.currentTick=held.currentTick; update.holdMs=held.holdMs;
            update.currentSourceSequence=held.currentSourceSequence;
            update.translationEnabled=update.previousOwnsCurrentAnalysis=true;
            update.retiringTranslationFitInspection=true;
            return update;
        }

		TransitionAdmissionInput MovingRecoveryObservation(const ActivePictureBounds& base,
			uint64_t generation,uint64_t sequence,unsigned index,double hz)
		{
			TransitionAdmissionInput input;
			input.trustedGeometry=input.presentationBeforeObservation=base;
			input.trustedGeometryAvailable=input.compatiblePresentation=input.evidence.available=true;
			input.trustedGeneration=input.sourceGeneration=input.presentationEvidenceGeneration=generation;
			input.sourceSequence=sequence; input.framesPerSecond=hz;
			auto target=base; target.top-=8+int(index)*4; target.bottom+=8+int(index)*4;
			target.aspectRatio=double(target.right-target.left)/(target.bottom-target.top);
			input.evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
			input.evidence.trustedBounds=input.evidence.proposedBounds=input.outwardCandidate=target;
			input.retention.analysisValid=input.retention.presentationValid=input.retention.expansionStripsAvailable=true;
			input.retention.expansionBase=base; input.retention.expansionCandidate=target;
			auto& edge=input.retention.expandingTop;
			edge.barPixels=8+int(index)*4; edge.blackFraction=.2; edge.continuity=.4; edge.lumaP90=300;
			input.retention.expandingBottom=edge;
			return input;
		}
		KnownFullRasterRetentionInput CommittedFullRasterRetention(uint64_t sequence = 100)
		{
			KnownFullRasterRetentionInput input;
			input.analysisValid = input.measurementCurrent = true;
			input.sourceGeneration = input.committedSourceGeneration = 7;
			input.presentationEpoch = input.committedPresentationEpoch = 3;
			input.sourceSequence = input.committedSourceSequence = sequence;
			input.frameWidth = 3840;
			input.frameHeight = 2160;
			input.rawClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
			input.rawBounds = input.committedBounds = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, ActivePictureBounds::BarAxes::NONE };
			input.committedFullAvailable = true;
			return input;
		}



		KnownFullRasterRetentionInput ReaffirmingCommittedFullRaster()
		{
			auto input = CommittedFullRasterRetention(100);
			input.previous = UpdateKnownFullRasterRetention(input);
			input.sourceSequence = 101;
			input.rawClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.previous = UpdateKnownFullRasterRetention(input);
			input.sourceSequence = 102;
			input.rawClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
			input.nearBlackEvaluated = true;
			return input;
		}

		struct FullRasterReplay
		{
			SceneDetector detector;
			ActivePictureTransitionModel transitionModel;
			KnownFullRasterRetentionState retention;
			NearBlackPresentationEpisodeState episode;
			ActivePictureBounds geometry;
			ActivePictureEvidence raw;
			ActivePictureTransitionDecision transition;
			SceneDetectorResult scene;
			bool geometryAvailable = false;
			bool fullAuthority = false;
			bool darknessExemption = false;
			uint64_t commitSequence = 0;

			void Frame(uint64_t sequence, int level, bool scope = false)
			{
				// Real 10-bit source: scope has 24-pixel encoded bars at Y64.
				std::vector<uint16_t> pixels(320 * 180 * 3 / 2, uint16_t(512 << 6));
				for (int y = 0; y < 180; ++y)
					for (int x = 0; x < 320; ++x)
						pixels[y * 320 + x] = uint16_t((scope && (y < 24 || y >= 156) ? 64 : level) << 6);
				AnalysisLumaSource source{reinterpret_cast<const uint8_t*>(pixels.data()),
					pixels.size() * sizeof(uint16_t), 320, 180, 640, 640,
					AnalysisLumaFormat::P010, VideoFrameEncoding::V210, ColorSpace::REC_709, 1};
				scene = detector.Analyze({pixels.data(), 320, 180, 640, sequence,
					static_cast<int64_t>(sequence * 416667), 1, 416667, true, &source});
				raw = ExtractActivePictureEvidence(source);
				const auto darkness = EvaluateActivePictureGlobalNearBlack(source);
				if (scene.safeBoundary) transitionModel.ResetCandidateEvidence();
				const auto evidence = ConstrainNearBlackCropAcquisition(raw,
					darkness.nearBlack || episode.mode != NearBlackPresentationMode::INACTIVE);
				transition = transitionModel.Observe(MakeActivePictureObservation(evidence, sequence, 24.0));
				if (transition.publish && transition.stable)
				{
					geometry = transition.bounds;
					geometryAvailable = true;
					if (transition.authoritativeClassification == ActivePictureClassification::FULL_RASTER_TRUSTED)
						commitSequence = sequence;
				}
				fullAuthority = UpdateFullRasterPresentationAuthority(fullAuthority, evidence.classification,
					evidence.trustedBounds.left == 0 && evidence.trustedBounds.top == 0 &&
					evidence.trustedBounds.right == 320 && evidence.trustedBounds.bottom == 180);
				KnownFullRasterRetentionInput input;
				input.previous = retention;
				input.analysisValid = source.IsValid();
				input.nearBlackEvaluated = darkness.evaluated;
				input.globalNearBlack = darkness.nearBlack;
				input.measurementCurrent = true;
				input.rawClassification = raw.classification;
				input.rawBounds = raw.trustedBounds;
				input.frameWidth = 320;
				input.frameHeight = 180;
				input.sourceGeneration = input.committedSourceGeneration = 1;
				input.presentationEpoch = input.committedPresentationEpoch = 1;
				input.sourceSequence = sequence;
				input.committedFullAvailable = geometryAvailable;
				input.committedBounds = geometry;
				input.committedSourceSequence = commitSequence;
				KnownFullRasterDarknessBoundaryInput boundary;
				boundary.retention = input;
				boundary.safeBoundary = scene.safeBoundary;
				boundary.nearBlackEntry = scene.nearBlackEntry;
				boundary.differenceEvaluated = scene.differenceEvaluated;
				boundary.hardCutCandidate = scene.hardCutCandidate;
				boundary.hardCutConfirmed = scene.hardCutConfirmed;
				darknessExemption = CanRetainKnownFullRasterAtDarknessBoundary(boundary);
				if (scene.safeBoundary)
				{
					SceneInput routing;
					routing.geometryAvailable = geometryAvailable;
					routing.geometryIsCurrentGeneration = routing.latestEvidenceIsCurrent = true;
					routing.geometryClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
					routing.latestClassification = evidence.classification;
					routing.knownFullRasterDarknessRetention = darknessExemption;
					if (EvaluateSceneBoundary(routing).action == ScenePresentationAction::WITHDRAW)
					{
						geometryAvailable = false;
						transitionModel.Reset();
					}
				}
				input.analysisValid = input.committedFullAvailable = geometryAvailable;
				// The renderer calls this exact adapter: cooldown cannot be
				// hidden by reproducing different scene wiring in this test.
				retention = UpdateKnownFullRasterRetentionForScene(input, scene, darknessExemption);
				NearBlackPresentationEpisodeInput ep;
				ep.previous = episode;
				ep.measurementCurrent = true;
				ep.nearBlackEvaluated = darkness.evaluated;
				ep.globalNearBlack = darkness.nearBlack;
				ep.sourceGeneration = ep.presentationEpoch = 1;
				ep.sourceSequence = sequence;
				ep.fullRasterAuthorityAvailable = fullAuthority;
				ep.knownFullRasterRetained = retention.available;
				ep.sceneBoundary = input.sceneBoundary;
				episode = EvaluateNearBlackPresentationEpisode(ep).state;
			}
		};

		void AssertFullRaster(const Decision& decision, int width = 3840, int height = 2160)
		{
			Assert::IsFalse(decision.applyCrop);
			Assert::AreEqual(0, decision.sourceBounds.left);
			Assert::AreEqual(0, decision.sourceBounds.top);
			Assert::AreEqual(width, decision.sourceBounds.right);
			Assert::AreEqual(height, decision.sourceBounds.bottom);
		}

		NearBlackPresentationEpisodeInput ReaffirmedRetainedScope(bool outwardAtEntry = false)
		{
			NearBlackPresentationEpisodeInput input;
			input.trustedCrop = { 0, 280, 3840, 1880, 3840, 2160, 2.4,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.measurementCurrent = input.nearBlackEvaluated = true;
			input.globalNearBlack = input.trustedCropAvailable = true;
			input.sourceGeneration = 1;
			input.presentationEpoch = 10;
			input.sourceSequence = 1962;
			input.framesPerSecond = 23.976;
			input.boundedVisibleContentOutsideCrop = outwardAtEntry;
			input.previous = EvaluateNearBlackPresentationEpisode(input).state;
			input.boundedVisibleContentOutsideCrop = false;
			input.globalNearBlack = false;
			input.currentObservationAvailable = input.retentionEvaluated = true;
			input.currentObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.currentObservation = input.retentionBounds = input.trustedCrop;
			input.retentionSafe = input.retentionExcludedBandsPixelSafe = true;
			input.retentionSourceGeneration = input.sourceGeneration;
			input.knownTrustedGeometryReacquired = input.reacquisitionIsCurrentAssociation = true;
			input.reacquiredTrustedGeometry = input.trustedCrop;
			input.reacquiredTrustedClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.reacquiredSourceGeneration = input.sourceGeneration;
			input.reacquiredPresentationEpoch = input.presentationEpoch;
			return input;
		}
	}

	TEST_CLASS(AlphaSourceCropPolicyTests)
	{
	public:
        static TransitionAdmissionInput ContradictedPillarboxInput()
        {
            TransitionAdmissionInput i;
            i.trustedGeometry=i.presentationBeforeObservation={136,0,3704,2160,3840,2160,3568.0/2160,ActivePictureBounds::BarAxes::LEFT_RIGHT};
            i.evidence.available=i.trustedGeometryAvailable=i.compatiblePresentation=true;
            i.evidence.classification=ActivePictureClassification::PROVISIONAL;
            i.evidence.proposedBounds={0,0,3840,2142,3840,2160,3840.0/2142,ActivePictureBounds::BarAxes::NONE};
            i.outwardCandidate=i.evidence.proposedBounds;
            i.evidence.axisEvidence.horizontal.scanComplete=true;
            i.evidence.axisEvidence.horizontal.state=ActivePictureAxisState::UNKNOWN;
            i.trustedGeneration=i.sourceGeneration=2; i.sourceSequence=100;
            i.retention.analysisValid=i.retention.presentationValid=true;
            i.retention.excludedLeft.barPixels=136;
            i.retention.excludedLeft.blackFraction=0.1;
            i.retention.excludedLeft.continuity=0.15;
            i.retention.excludedLeft.lumaP90=372;
            i.retention.excludedRight=i.retention.excludedLeft;
            return i;
        }

        TEST_METHOD(FullWidthPictureWithdrawsPillarboxDespiteUncertainVerticalEdges)
        {
            auto i=ContradictedPillarboxInput();
            for(uint64_t seq=100;seq<=102;++seq)
            {
                i.sourceSequence=seq;
                i.evidence.proposedBounds.bottom=seq%2 ? 2144 : 2142;
                // The vertical candidate's certificate is distinct from the old side bands.
                i.retention.expansionStripsAvailable=true;
                i.retention.expansionBase=i.trustedGeometry;
                i.retention.expansionCandidate=i.evidence.proposedBounds;
                const auto d=EvaluateTransitionAdmission(i);
                Assert::AreEqual(seq==102,d.pillarboxContradiction.authoritative);
                Assert::IsTrue(d.observation.classification==ActivePictureClassification::PROVISIONAL);
                if(seq==102) Assert::IsTrue(d.observation.transitionDeferred);
                i.previousPillarboxContradiction=d.pillarboxContradiction.state;
            }
        }

        TEST_METHOD(PillarboxWithdrawalRejectsDarknessOverlaysStaleAndIncompleteEvidence)
        {
            for(int variant=0;variant<15;++variant)
            {
                auto i=ContradictedPillarboxInput();
                switch(variant)
                {
                case 0:i.retention.globalNearBlack=true;break;
                case 1:i.retention.excludedLeft.blackFraction=0.96;break;
                case 2:i.retention.excludedRight.continuity=0.99;break;
                case 3:i.evidence.axisEvidence.horizontal.scanComplete=false;break;
                case 4:i.evidence.axisEvidence.horizontal.barCandidate=true;break;
                case 5:i.trustedGeneration=1;break;
                case 6:i.evidence.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;break;
                case 7:i.retention.analysisValid=false;break;
                case 8:i.retention.presentationValid=false;break;
                case 9:i.presentationBeforeObservation.left+=2;break;
                case 10:i.retention.excludedLeft.barPixels-=2;break;
                case 11:i.trustedGeometry.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;break;
                case 12:i.evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;break;
                case 13:i.retention.excludedLeft.blackFraction=0.60;break;
                case 14:i.retention.excludedRight.blackFraction=0.60;break;
                }
                for(uint64_t seq=100;seq<120;++seq){i.sourceSequence=seq;const auto d=EvaluateTransitionAdmission(i);Assert::IsFalse(d.pillarboxContradiction.authoritative);i.previousPillarboxContradiction=d.pillarboxContradiction.state;}
            }
        }

        TEST_METHOD(PillarboxWithdrawalRequiresAdjacentUniqueSourceObservations)
        {
            auto i=ContradictedPillarboxInput();
            auto d=EvaluateTransitionAdmission(i); i.previousPillarboxContradiction=d.pillarboxContradiction.state;
            for(int repeat=0;repeat<20;++repeat){d=EvaluateTransitionAdmission(i);Assert::AreEqual(1u,d.pillarboxContradiction.state.confirmations);i.previousPillarboxContradiction=d.pillarboxContradiction.state;}
            i.sourceSequence=102;d=EvaluateTransitionAdmission(i);Assert::AreEqual(1u,d.pillarboxContradiction.state.confirmations);i.previousPillarboxContradiction=d.pillarboxContradiction.state;
            i.sourceGeneration=i.trustedGeneration=3;i.sourceSequence=103;d=EvaluateTransitionAdmission(i);Assert::AreEqual(1u,d.pillarboxContradiction.state.confirmations);i.previousPillarboxContradiction=d.pillarboxContradiction.state;
            i.sourceSequence=104;d=EvaluateTransitionAdmission(i);Assert::IsFalse(d.pillarboxContradiction.authoritative);i.previousPillarboxContradiction=d.pillarboxContradiction.state;
            i.sourceSequence=105;Assert::IsTrue(EvaluateTransitionAdmission(i).pillarboxContradiction.authoritative);
        }

        struct DarkSubtitleFixture
        {
            NearBlackPresentationEpisodeInput episode;
            CropPresentationAdmissionState admission;
            EstablishedSubtitleTranslationInput translation;
            DarkSubtitleFixture()
            {
                episode = ReaffirmedRetainedScope();
                episode.previous = {};
                episode.trustedCrop = {0,276,3840,1884,3840,2160,2.388,
                    ActivePictureBounds::BarAxes::TOP_BOTTOM};
                episode.retentionBounds = episode.trustedCrop;
                episode.retentionSourceSequence = episode.sourceSequence;
                episode.currentObservation = episode.trustedCrop;
                episode.currentObservation.right = 3818;
                episode.currentObservation.bottom = 1902;
                episode.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
                episode.retentionSafe = episode.retentionExcludedBandsPixelSafe = false;
                episode.boundedVisibleContentOutsideCrop = true;
                episode.globalNearBlack = true;
                episode.sourceGeneration = episode.retentionSourceGeneration = 2;
                episode.sourceSequence = episode.retentionSourceSequence = 913;
                episode.currentTick = 1000;
                admission.available = true;
                admission.trustedCrop = episode.trustedCrop;
                admission.sourceGeneration = episode.sourceGeneration;
                admission.presentationEpoch = episode.presentationEpoch;
                translation.base = episode.trustedCrop;
                translation.visibleBounds = episode.trustedCrop;
                translation.visibleBounds.bottom = 1937;
                translation.sourceGeneration = episode.sourceGeneration;
                translation.holdMs = 5000;
                translation.resolvedTranslationPixels = 70;
                translation.presentation.action = VerticalBarPresentationAction::TRANSLATE;
                translation.presentation.translationPixels = 70;
                translation.presentation.detectedBottom = 1930;
                translation.presentation.sourceSequence = episode.sourceSequence - 1;
                translation.presentation.lastDetectionTick = 1000;
            }
            bool Certificate() const
            {
                return CanRetainEstablishedScopeForSubtitle(episode, admission, translation);
            }
        };


        TEST_METHOD(DarkSubtitleLiveOwnerSurvivesPixelSafeEmptyBarSample)
        {
            DarkSubtitleFixture f;
            f.episode.sourceSequence = f.episode.retentionSourceSequence = 751;
            f.translation.presentation.sourceSequence = 750;
            f.translation.presentation.translationPixels = 182;
            f.translation.resolvedTranslationPixels = 182;
            f.translation.visibleBounds = f.episode.trustedCrop;
            f.translation.visibleBounds.bottom = 2023;
            f.episode.currentObservation = f.translation.visibleBounds;
            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = f.episode.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.verticalTranslationActive = true;
            crop.verticalTranslationPixels = 182;
            auto advance = [&](uint64_t sequence, uint64_t tick, bool outward,
                bool bandsSafe, const ActivePictureBounds& observation) {
                f.episode.sourceSequence = f.episode.retentionSourceSequence = sequence;
                f.episode.currentTick = tick;
                f.episode.boundedVisibleContentOutsideCrop = outward;
                f.episode.retentionSafe = bandsSafe;
                f.episode.retentionExcludedBandsPixelSafe = bandsSafe;
                f.episode.currentObservation = observation;
                f.episode.currentNativeObservationAvailable = true;
                f.episode.currentNativeObservation = observation;
                f.episode.currentObservationClassification = outward
                    ? ActivePictureClassification::PROVISIONAL
                    : ActivePictureClassification::BAR_CROP_TRUSTED;
                f.translation.visibleBounds = outward ? observation : ActivePictureBounds{};
                f.episode.establishedScopeSubtitleRetained = f.Certificate();
                const auto episode = EvaluateNearBlackPresentationEpisode(f.episode);
                crop.frameSourceSequence = sequence;
                crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(episode);
                crop.nearBlackEpisodeFullRaster = episode.state.mode == NearBlackPresentationMode::FULL_RASTER;
                crop.verticalTranslationActive = !crop.nearBlackEpisodeRetainCrop;
                crop.frameLocalPresentationRetentionSafe = bandsSafe;
                const auto admitted = AdmitCropPresentation(f.admission, crop, Evaluate(crop),
                    f.episode.presentationEpoch);
                f.admission = admitted.state;
                f.episode.previous = episode.state;
                return admitted.presentation;
            };
            auto outward = f.episode.trustedCrop;
            outward.bottom = 2023;
            const auto first = advance(751, 1000, true, false, outward);
            Assert::IsTrue(f.episode.establishedScopeSubtitleRetained);
            Assert::IsTrue(first.verticallyTranslated);
            Assert::AreEqual(458, first.sourceBounds.top);
            Assert::AreEqual(2066, first.sourceBounds.bottom);

            // Current native bounds equal the trusted base; the omitted
            // outward rectangle reflects a clean, pixel-safe subtitle sample.
            const auto clean = advance(787, 1800, false, true, f.episode.trustedCrop);
            Assert::IsTrue(f.episode.establishedScopeSubtitleRetained);
            Assert::IsTrue(clean.verticallyTranslated);
            Assert::AreEqual(458, clean.sourceBounds.top);
            Assert::AreEqual(2066, clean.sourceBounds.bottom);

            outward.bottom = 2045;
            const auto returned = advance(788, 1831, true, false, outward);
            Assert::IsTrue(f.episode.establishedScopeSubtitleRetained);
            Assert::AreEqual(458, returned.sourceBounds.top);
            Assert::AreEqual(2066, returned.sourceBounds.bottom);
        }

        TEST_METHOD(DarkSubtitleInwardNativeSampleKeepsAdmittedTranslation)
        {
            DarkSubtitleFixture f;
            const ActivePictureBounds base = {0,272,3840,1888,3840,2160,2.388,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            const ActivePictureBounds inward = {0,276,3840,1884,3840,2160,2.388,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            f.episode.trustedCrop = f.episode.retentionBounds = base;
            f.admission.trustedCrop = f.translation.base = base;
            f.episode.sourceSequence = f.episode.retentionSourceSequence = 49582;
            f.episode.currentTick = 1000;
            f.translation.presentation.sourceSequence = 49581;
            f.translation.presentation.lastDetectionTick = 1000;
            f.translation.presentation.translationPixels = 126;
            f.translation.resolvedTranslationPixels = 126;
            f.translation.visibleBounds = base;
            f.translation.visibleBounds.bottom = 2010;
            f.episode.currentObservation = f.translation.visibleBounds;
            f.episode.boundedVisibleContentOutsideCrop = true;
            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = base;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.frameSourceSequence = 49582;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.verticalTranslationActive = true;
            crop.verticalTranslationPixels = 126;
            f.episode.establishedScopeSubtitleRetained = f.Certificate();
            Assert::IsTrue(f.episode.establishedScopeSubtitleRetained);
            auto episode = EvaluateNearBlackPresentationEpisode(f.episode);
            crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(episode);
            auto admitted = AdmitCropPresentation(f.admission, crop, Evaluate(crop),
                f.episode.presentationEpoch);
            Assert::IsTrue(admitted.presentation.verticallyTranslated);
            Assert::AreEqual(398, admitted.presentation.sourceBounds.top);
            Assert::AreEqual(2014, admitted.presentation.sourceBounds.bottom);
            f.admission = admitted.state;
            f.episode.previous = episode.state;

            // The detector's guarded trusted base extends four rows beyond
            // the current native observation on both edges. The excluded
            // bands are freshly pixel-safe and no outward content is present.
            f.episode.sourceSequence = f.episode.retentionSourceSequence = 49583;
            f.episode.currentTick = 1047;
            f.episode.currentObservation = f.episode.currentNativeObservation = inward;
            f.episode.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
            f.episode.currentObservationOrigin = ActivePictureAuthorityOrigin::NATIVE;
            f.episode.currentNativeObservationAvailable = true;
            f.episode.boundedVisibleContentOutsideCrop = false;
            f.episode.retentionSafe = f.episode.retentionExcludedBandsPixelSafe = true;
            f.translation.visibleBounds = {};
            f.translation.resolvedTranslationPixels = 124;
            Assert::IsTrue(f.Certificate(),
                L"Fresh inward native bars must not revoke a live admitted translation.");
            f.episode.establishedScopeSubtitleRetained = f.Certificate();
            episode = EvaluateNearBlackPresentationEpisode(f.episode);
            Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::INACTIVE),
                static_cast<int>(episode.state.mode));
            crop.frameSourceSequence = 49583;
            crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(episode);
            crop.verticalTranslationPixels = 124;
            crop.frameLocalPresentationRetentionSafe = true;
            admitted = AdmitCropPresentation(f.admission, crop, Evaluate(crop),
                f.episode.presentationEpoch);
            Assert::IsTrue(admitted.presentation.verticallyTranslated);
            Assert::AreEqual(396, admitted.presentation.sourceBounds.top);
            Assert::AreEqual(2012, admitted.presentation.sourceBounds.bottom);

            // These samples cannot borrow the prior owner or use a clean-band
            // proof to hide outward pixels or a different source identity.
            for (int variant = 0; variant < 14; ++variant)
            {
                auto bad = f;
                switch (variant)
                {
                case 0: bad.episode.retentionSafe = false; break;
                case 1: bad.episode.retentionExcludedBandsPixelSafe = false; break;
                case 2: bad.episode.currentNativeObservationAvailable = false; break;
                case 3: bad.episode.currentNativeObservation.top = base.top - 2; break;
                case 4: bad.episode.currentObservation.bottom = base.bottom + 2; break;
                case 5: bad.episode.currentObservation.left = 2; break;
                case 6: bad.episode.currentNativeObservation.trustedBarAxes =
                    ActivePictureBounds::BarAxes::NONE; break;
                case 7: bad.episode.currentObservationOrigin =
                    ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT; break;
                case 8: bad.episode.boundedVisibleContentOutsideCrop = true;
                    bad.translation.visibleBounds = inward;
                    bad.translation.visibleBounds.top = base.top - 2; break;
                case 9: bad.episode.sceneBoundary = true; break;
                case 10: bad.episode.fullRasterAuthorityAvailable = true; break;
                case 11: ++bad.admission.presentationEpoch; break;
                case 12: ++bad.episode.retentionSourceGeneration; break;
                case 13: bad.episode.currentTick = 7000; break;
                }
                Assert::IsFalse(bad.Certificate(),
                    L"Inward observation needs current native pixels and the same live owner.");
            }
            auto pending = f;
            pending.translation.presentation.action = VerticalBarPresentationAction::NONE;
            pending.episode.subtitleTranslationConfirmation.confirmations = 1;
            pending.episode.subtitleTranslationConfirmation.lastObservedSourceSequence =
                pending.episode.sourceSequence;
            pending.episode.subtitleTranslationConfirmation.candidateTranslationPixels = 124;
            pending.episode.subtitleInspectionGeneration = pending.episode.sourceGeneration;
            pending.episode.subtitleInspectionBars = base;
            Assert::IsFalse(pending.Certificate(),
                L"A pending caption cannot acquire translation from a clean inward sample.");
        }
        TEST_METHOD(DarkSubtitleInwardSamplesFollowCertifiedReleaseWithoutRenewal)
        {
            DarkSubtitleFixture f;
            const ActivePictureBounds base = {0,272,3840,1888,3840,2160,2.388,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            const ActivePictureBounds inward = {0,276,3840,1884,3840,2160,2.388,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            f.episode.trustedCrop = f.episode.retentionBounds = base;
            f.admission.trustedCrop = f.translation.base = base;
            f.episode.sourceSequence = f.episode.retentionSourceSequence = 49582;
            f.episode.currentTick = 1000;
            f.translation.holdMs = 2000;
            f.translation.presentation.sourceSequence = 49582;
            f.translation.presentation.lastDetectionTick = 1000;
            f.translation.presentation.translationPixels = 126;
            f.translation.resolvedTranslationPixels = 126;
            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = base;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.frameSourceSequence = 49582;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.verticalTranslationActive = true;
            crop.verticalTranslationPixels = 126;
            const auto first = AdmitCropPresentation(f.admission, crop, Evaluate(crop),
                f.episode.presentationEpoch);
            Assert::IsTrue(first.presentation.verticallyTranslated);
            f.admission = first.state;

            VerticalTranslationDrift drift;
            drift.Resolve(126.0f, 1000, 0);
            drift.Resolve(0.0f, 3100, 2000);
            f.translation.releaseOwner = f.translation.presentation;
            f.translation.releaseOwnerBase = base;
            f.translation.releaseSourceGeneration = f.episode.sourceGeneration;
            f.translation.releasePresentationEpoch = f.episode.presentationEpoch;
            f.translation.releaseStartedTick = 3100;
            f.translation.releaseDurationMs = 2000;
            f.translation.presentation.action = VerticalBarPresentationAction::NONE;
            f.translation.visibleBounds = {};
            int previousTop = first.presentation.sourceBounds.top;
            const uint64_t ticks[] = {3500, 3900, 4300, 4700};
            for (int i = 0; i < 4; ++i)
            {
                const float applied = drift.Resolve(0.0f, ticks[i], 2000);
                VerticalBarPresentationResolutionInput resolution;
                resolution.detailedAction = VerticalBarPresentationAction::TRANSLATE;
                resolution.translationPixels = applied;
                resolution.authoritativeTop = base.top;
                resolution.authoritativeBottom = base.bottom;
                resolution.rasterHeight = base.rasterHeight;
                const auto routed = ResolveVerticalBarRendererRouting(
                    ResolveVerticalBarPresentation(resolution));
                Assert::IsTrue(routed.translationActive && routed.translationPixels > 0);
                f.episode.sourceSequence = f.episode.retentionSourceSequence = 49583 + i;
                f.episode.currentTick = ticks[i];
                f.episode.currentObservation = f.episode.currentNativeObservation =
                    i % 2 == 0 ? inward : base;
                f.episode.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
                f.episode.currentNativeObservationAvailable = true;
                f.episode.boundedVisibleContentOutsideCrop = false;
                f.episode.retentionSafe = f.episode.retentionExcludedBandsPixelSafe = true;
                f.translation.releaseIntervalAvailable = drift.TryGetActiveInterval(
                    f.translation.releaseStartTranslationPixels,
                    f.translation.releaseTargetTranslationPixels,
                    f.translation.releaseAppliedTranslationPixels);
                f.translation.resolvedTranslationPixels = routed.translationPixels;
                Assert::IsTrue(f.translation.releaseIntervalAvailable);
                Assert::IsTrue(f.Certificate(),
                    L"A certified release must keep each current safe inward or exact-base pose.");
                f.episode.establishedScopeSubtitleRetained = true;
                const auto episode = EvaluateNearBlackPresentationEpisode(f.episode);
                Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE), int(episode.state.mode));
                crop.frameSourceSequence = f.episode.sourceSequence;
                crop.verticalTranslationPixels = routed.translationPixels;
                crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(episode);
                const auto admitted = AdmitCropPresentation(f.admission, crop, Evaluate(crop),
                    f.episode.presentationEpoch);
                Assert::IsTrue(admitted.presentation.verticallyTranslated);
                Assert::AreEqual(base.top + routed.translationPixels,
                    admitted.presentation.sourceBounds.top);
                Assert::IsTrue(admitted.presentation.sourceBounds.top <= previousTop);
                Assert::IsTrue(admitted.presentation.sourceBounds.top > base.top);
                previousTop = admitted.presentation.sourceBounds.top;
                f.admission = admitted.state;
                f.episode.previous = episode.state;
                Assert::AreEqual(uint64_t(1000), f.translation.releaseOwner.lastDetectionTick);
            }
            // Completion settles at the trusted base. The old owner cannot
            // restart or extend the release after its finite interval ends.
            drift.Resolve(0.0f, 5100, 2000);
            f.episode.sourceSequence = f.episode.retentionSourceSequence = 49587;
            f.episode.currentTick = 5100;
            f.episode.currentObservation = f.episode.currentNativeObservation = inward;
            f.translation.releaseIntervalAvailable = drift.TryGetActiveInterval(
                f.translation.releaseStartTranslationPixels,
                f.translation.releaseTargetTranslationPixels,
                f.translation.releaseAppliedTranslationPixels);
            f.translation.resolvedTranslationPixels = 0;
            Assert::IsFalse(f.Certificate());
            f.episode.establishedScopeSubtitleRetained = false;
            const auto settled = EvaluateNearBlackPresentationEpisode(f.episode);
            crop.frameSourceSequence = f.episode.sourceSequence;
            crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(settled);
            crop.verticalTranslationActive = false;
            crop.verticalTranslationPixels = 0;
            const auto atBase = AdmitCropPresentation(f.admission, crop, Evaluate(crop),
                f.episode.presentationEpoch);
            Assert::AreEqual(base.top, atBase.presentation.sourceBounds.top);
            Assert::AreEqual(base.bottom, atBase.presentation.sourceBounds.bottom);
        }
        TEST_METHOD(DarkSubtitleEmptyBarRequiresCurrentSafeLiveOwner)
        {
            DarkSubtitleFixture seed;
            seed.episode.sourceSequence = seed.episode.retentionSourceSequence = 787;
            seed.episode.currentTick = 1800;
            seed.translation.presentation.sourceSequence = 751;
            seed.translation.presentation.lastDetectionTick = 1000;
            seed.translation.presentation.translationPixels = 182;
            seed.translation.resolvedTranslationPixels = 182;
            seed.episode.boundedVisibleContentOutsideCrop = false;
            seed.translation.visibleBounds = {};
            seed.episode.currentObservation = seed.episode.trustedCrop;
            seed.episode.currentObservationClassification =
                ActivePictureClassification::BAR_CROP_TRUSTED;
            seed.episode.currentNativeObservationAvailable = true;
            seed.episode.currentNativeObservation = seed.episode.trustedCrop;
            seed.episode.retentionSafe = true;
            seed.episode.retentionExcludedBandsPixelSafe = true;
            Assert::IsTrue(seed.Certificate());

            for (int variant = 0; variant < 11; ++variant)
            {
                auto f = seed;
                switch (variant)
                {
                case 0: f.episode.retentionSafe = false; break;
                case 1: f.episode.retentionExcludedBandsPixelSafe = false; break;
                case 2: f.episode.currentNativeObservationAvailable = false; break;
                case 3: --f.episode.currentNativeObservation.top; break;
                case 4: ++f.episode.currentObservation.bottom; break;
                case 5: --f.episode.retentionSourceSequence; break;
                case 6: ++f.episode.retentionSourceGeneration; break;
                case 7: ++f.admission.presentationEpoch; break;
                case 8: f.episode.currentTick = 7000; break;
                case 9: f.translation.presentation.action =
                    VerticalBarPresentationAction::NONE;
                    f.episode.subtitleTranslationConfirmation.confirmations = 1;
                    f.episode.subtitleTranslationConfirmation.lastObservedSourceSequence =
                        f.episode.sourceSequence;
                    f.episode.subtitleTranslationConfirmation.candidateTranslationPixels = 182;
                    f.episode.subtitleInspectionGeneration = f.episode.sourceGeneration;
                    f.episode.subtitleInspectionBars = f.episode.trustedCrop;
                    break;
                case 10: f.episode.sceneBoundary = true; break;
                }
                Assert::IsFalse(f.Certificate(),
                    L"Empty-bar retention requires exact current pixels and a live confirmed owner.");
            }

            auto intrusion = seed;
            intrusion.episode.boundedVisibleContentOutsideCrop = true;
            intrusion.translation.visibleBounds = intrusion.episode.trustedCrop;
            intrusion.translation.visibleBounds.top = 275;
            intrusion.translation.visibleBounds.bottom = 2023;
            intrusion.episode.currentObservation = intrusion.translation.visibleBounds;
            Assert::IsFalse(intrusion.Certificate(),
                L"Opposite-edge picture pixels must veto retained subtitle translation.");
        }

        TEST_METHOD(DarkSubtitleReleaseDriftKeepsCertifiedScopeOnSafeEmptyBars)
        {
            DarkSubtitleFixture f;
            f.episode.sourceSequence = f.episode.retentionSourceSequence = 751;
            f.episode.currentTick = 1000;
            f.translation.holdMs = 2000;
            f.translation.presentation.sourceSequence = 751;
            f.translation.presentation.lastDetectionTick = 1000;
            f.translation.presentation.translationPixels = 182;
            f.translation.resolvedTranslationPixels = 182;
            f.translation.visibleBounds = f.episode.trustedCrop;
            f.translation.visibleBounds.bottom = 2023;
            f.episode.currentObservation = f.translation.visibleBounds;
            Assert::IsTrue(f.Certificate());
            f.episode.establishedScopeSubtitleRetained = true;
            f.episode.previous = EvaluateNearBlackPresentationEpisode(f.episode).state;

            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = f.episode.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.verticalTranslationActive = true;
            crop.verticalTranslationPixels = 182;
            crop.frameSourceSequence = 751;
            const auto admitted = AdmitCropPresentation(f.admission, crop, Evaluate(crop),
                f.episode.presentationEpoch);
            Assert::IsFalse(admitted.blocked);
            Assert::IsTrue(admitted.presentation.verticallyTranslated);
            f.admission = admitted.state;

            VerticalTranslationDrift drift;
            drift.Resolve(182.0f, 1000, 0);
            drift.Resolve(0.0f, 3100, 2000);
            const float applied = drift.Resolve(0.0f, 3500, 2000);
            VerticalBarPresentationResolutionInput resolution;
            resolution.detailedAction = VerticalBarPresentationAction::TRANSLATE;
            resolution.translationPixels = applied;
            resolution.authoritativeTop = f.episode.trustedCrop.top;
            resolution.authoritativeBottom = f.episode.trustedCrop.bottom;
            resolution.rasterHeight = f.episode.trustedCrop.rasterHeight;
            const auto routed = ResolveVerticalBarRendererRouting(
                ResolveVerticalBarPresentation(resolution));
            Assert::IsTrue(drift.IsActive() && routed.translationActive);
            Assert::IsTrue(routed.translationPixels > 0 &&
                routed.translationPixels < 182);

            f.episode.sourceSequence = f.episode.retentionSourceSequence = 850;
            f.episode.currentTick = 3500;
            f.episode.boundedVisibleContentOutsideCrop = false;
            f.episode.retentionSafe = f.episode.retentionExcludedBandsPixelSafe = true;
            f.episode.currentObservation = f.episode.currentNativeObservation =
                f.episode.trustedCrop;
            f.episode.currentNativeObservationAvailable = true;
            f.episode.currentObservationClassification =
                ActivePictureClassification::BAR_CROP_TRUSTED;
            f.translation.releaseOwner = f.translation.presentation;
            f.translation.releaseOwnerBase = f.episode.trustedCrop;
            f.translation.releaseSourceGeneration = f.episode.sourceGeneration;
            f.translation.releasePresentationEpoch = f.episode.presentationEpoch;
            f.translation.releaseStartedTick = 3100;
            f.translation.releaseDurationMs = 2000;
            f.translation.releaseIntervalAvailable = drift.TryGetActiveInterval(
                f.translation.releaseStartTranslationPixels,
                f.translation.releaseTargetTranslationPixels,
                f.translation.releaseAppliedTranslationPixels);
            f.translation.presentation.action = VerticalBarPresentationAction::NONE;
            f.translation.visibleBounds = {};
            f.translation.resolvedTranslationPixels = routed.translationPixels;
            Assert::IsTrue(f.Certificate(),
                L"A certified translation releasing through a safe empty bar frame must keep its current pose.");
            f.episode.establishedScopeSubtitleRetained = true;
            const auto episode = EvaluateNearBlackPresentationEpisode(f.episode);
            Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE),
                int(episode.state.mode));
            crop.frameSourceSequence = f.episode.sourceSequence;
            crop.verticalTranslationPixels = routed.translationPixels;
            const auto selected = AdmitCropPresentation(f.admission, crop,
                Evaluate(crop), f.episode.presentationEpoch);
            Assert::IsTrue(selected.presentation.verticallyTranslated);
            Assert::AreEqual(276 + routed.translationPixels,
                selected.presentation.sourceBounds.top);

            for (int variant = 0; variant < 14; ++variant)
            {
                auto invalid = f;
                switch (variant)
                {
                case 0: invalid.translation.releaseOwner = {}; break;
                case 1: ++invalid.translation.releaseSourceGeneration; break;
                case 2: ++invalid.translation.releasePresentationEpoch; break;
                case 3: ++invalid.translation.releaseOwnerBase.top; break;
                case 4: invalid.translation.releaseIntervalAvailable = false; break;
                case 5: invalid.translation.releaseTargetTranslationPixels = 10; break;
                case 6: invalid.translation.releaseAppliedTranslationPixels = 190; break;
                case 7: ++invalid.translation.resolvedTranslationPixels; break;
                case 8: invalid.translation.releaseOwner.action =
                    VerticalBarPresentationAction::FIT; break;
                case 9: invalid.episode.retentionExcludedBandsPixelSafe = false; break;
                case 10: --invalid.episode.currentNativeObservation.top; break;
                case 11: invalid.translation.releaseStartedTick = 2999; break;
                case 12: invalid.episode.currentTick = 6000; break;
                case 13: invalid.episode.sceneBoundary = true; break;
                }
                Assert::IsFalse(invalid.Certificate(),
                    L"Release retention requires the exact admitted owner, current safe pixels, and bounded drift.");
            }

            f.episode.sourceSequence = f.episode.retentionSourceSequence = 851;
            f.episode.currentTick = 5100;
            f.translation.releaseIntervalAvailable = false;
            f.translation.resolvedTranslationPixels = 0;
            Assert::IsFalse(f.Certificate());
            f.episode.establishedScopeSubtitleRetained = false;
            const auto completed = EvaluateNearBlackPresentationEpisode(f.episode);
            crop.frameSourceSequence = f.episode.sourceSequence;
            crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(completed);
            crop.verticalTranslationActive = !crop.nearBlackEpisodeRetainCrop;
            crop.verticalTranslationPixels = 0;
            const auto basePresentation = AdmitCropPresentation(selected.state,
                crop, Evaluate(crop), f.episode.presentationEpoch);
            Assert::IsTrue(basePresentation.presentation.applyCrop);
            Assert::AreEqual(276, basePresentation.presentation.sourceBounds.top);
            Assert::AreEqual(1884, basePresentation.presentation.sourceBounds.bottom);
        }
        TEST_METHOD(DarkStarfieldCannotPreemptEstablishedCaptionTranslation)
        {
            DarkSubtitleFixture f;
            // Exact failure shape: confirmed scope and translation, caption-only
            // bottom witness, a provisional darker right edge, then p90 crosses
            // the global-near-black cutoff on a skipped dense-analysis frame.
            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = f.episode.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            crop.verticalTranslationActive = true;
            crop.verticalTranslationPixels = 70;
            for (int frame = 0; frame < 80; ++frame)
            {
                f.episode.globalNearBlack = frame % 3 == 0;
                f.episode.retentionSourceSequence = f.episode.sourceSequence;
                f.episode.establishedScopeSubtitleRetained = f.Certificate();
                Assert::IsTrue(f.episode.establishedScopeSubtitleRetained);
                const auto decision = EvaluateNearBlackPresentationEpisode(f.episode);
                Assert::IsTrue(decision.state.mode == NearBlackPresentationMode::INACTIVE);
                crop.frameSourceSequence = f.episode.sourceSequence;
                crop.nearBlackEpisodeFullRaster = decision.state.mode == NearBlackPresentationMode::FULL_RASTER;
                const auto presented = Evaluate(crop);
                Assert::IsTrue(presented.applyCrop && presented.verticallyTranslated);
                Assert::AreEqual(1608, presented.sourceBounds.bottom - presented.sourceBounds.top);
                Assert::AreEqual(1954, presented.sourceBounds.bottom);
                f.episode.previous = decision.state;
                ++f.episode.sourceSequence;
                f.episode.currentTick += 42;
            }
        }



        TEST_METHOD(DarkEngageUsesRoutedAlignedPoseForScopeCertificate)
        {
            const int elapsedSamples[] = {0, 1, 50, 100, 150, 200, 202,
                250, 300, 350, 400, 450, 500};
            for (int sign : {1, -1})
            {
                DarkSubtitleFixture f;
                const int target = sign * 170;
                f.translation.presentation.translationPixels = float(target);
                f.translation.presentation.sourceSequence = f.episode.sourceSequence - 1;
                f.translation.visibleBounds = f.episode.trustedCrop;
                f.episode.currentObservation = f.episode.trustedCrop;
                if (sign > 0)
                {
                    f.translation.visibleBounds.bottom = 2045;
                    f.episode.currentObservation.bottom = 2045;
                }
                else
                {
                    f.translation.visibleBounds.top = 115;
                    f.episode.currentObservation.top = 115;
                }

                VerticalTranslationDrift drift;
                bool sawFractionalAlignment = false;
                bool sawExactZero = false;
                for (unsigned i = 0; i < sizeof(elapsedSamples) / sizeof(elapsedSamples[0]); ++i)
                {
                    const int elapsed = elapsedSamples[i];
                    const float applied = drift.Resolve(float(target), 1000 + elapsed, 500);
                    VerticalBarPresentationResolutionInput resolution;
                    resolution.detailedAction = VerticalBarPresentationAction::TRANSLATE;
                    resolution.translationPixels = applied;
                    resolution.zeroTranslationRetainsTrustedBase = drift.IsActive();
                    resolution.authoritativeTop = f.episode.trustedCrop.top;
                    resolution.authoritativeBottom = f.episode.trustedCrop.bottom;
                    resolution.rasterHeight = 2160;
                    const auto routed = ResolveVerticalBarRendererRouting(
                        ResolveVerticalBarPresentation(resolution));

                    f.episode.sourceSequence = f.episode.retentionSourceSequence = 913 + i;
                    f.episode.currentTick = 1000 + elapsed;
                    f.translation.engagingTranslation = drift.IsActive();
                    f.translation.engageIntervalAvailable = f.translation.engagingTranslation &&
                        drift.TryGetActiveInterval(
                            f.translation.engageStartTranslationPixels,
                            f.translation.engageTargetTranslationPixels,
                            f.translation.engageAppliedTranslationPixels);
                    f.translation.resolvedTranslationPixels = routed.translationPixels;
                    sawFractionalAlignment |= f.translation.engagingTranslation &&
                        std::abs(double(routed.translationPixels) - double(applied)) > 1.0;
                    sawExactZero |= f.translation.engagingTranslation &&
                        routed.translationPixels == 0;
                    Assert::IsTrue(f.Certificate(),
                        L"Every routed engage pose must retain the current bounded subtitle scope.");
                }
                Assert::IsTrue(sawFractionalAlignment);
                Assert::IsTrue(sawExactZero);
            }
        }

        TEST_METHOD(DarkEngageRejectsPoseThatRoutesToFitOutsideRaster)
        {
            DarkSubtitleFixture f;
            f.episode.trustedCrop.bottom = 1893;
            f.episode.retentionBounds = f.episode.trustedCrop;
            f.admission.trustedCrop = f.episode.trustedCrop;
            f.translation.base = f.episode.trustedCrop;
            f.translation.visibleBounds = f.episode.currentObservation = f.episode.trustedCrop;
            f.translation.visibleBounds.bottom = f.episode.currentObservation.bottom = 2160;
            f.translation.presentation.translationPixels = 267.0f;
            f.translation.engagingTranslation = f.translation.engageIntervalAvailable = true;
            f.translation.engageStartTranslationPixels = 0.0f;
            f.translation.engageTargetTranslationPixels = 267.0f;
            f.translation.engageAppliedTranslationPixels = 266.1f;
            f.translation.resolvedTranslationPixels = 0;

            VerticalBarPresentationResolutionInput resolution;
            resolution.detailedAction = VerticalBarPresentationAction::TRANSLATE;
            resolution.translationPixels = f.translation.engageAppliedTranslationPixels;
            resolution.zeroTranslationRetainsTrustedBase = true;
            resolution.authoritativeTop = f.episode.trustedCrop.top;
            resolution.authoritativeBottom = f.episode.trustedCrop.bottom;
            resolution.rasterHeight = f.episode.trustedCrop.rasterHeight;
            const auto routed = ResolveVerticalBarRendererRouting(
                ResolveVerticalBarPresentation(resolution));
            Assert::IsTrue(routed.fitActive);
            Assert::AreEqual(0, routed.translationPixels);
            Assert::IsFalse(f.Certificate());
        }

        TEST_METHOD(NlsWithoutOwnCropKeepsConfirmedTranslationAcrossDarknessThreshold)
        {
            DarkSubtitleFixture f;
            f.episode.sourceSequence = 77;
            f.episode.retentionSourceSequence = 77;
            f.episode.currentObservation = f.episode.trustedCrop;
            f.episode.currentObservation.bottom = 2010;
            f.translation.visibleBounds = f.episode.currentObservation;
            f.translation.resolvedTranslationPixels = 162;
            f.translation.presentation.translationPixels = 162;
            f.translation.presentation.sourceSequence = 76;
            f.translation.competingPresentation =
                NlsCompetesWithEstablishedSubtitleTranslation(true, 0.0);
            Assert::IsFalse(f.translation.competingPresentation);

            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = f.episode.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            crop.verticalTranslationActive = true;
            crop.verticalTranslationPixels = 162;

            // The playback trace crossed P90=96 on alternating frames while
            // the native bars and translated scope stayed constant.
            const bool nearBlack[] = {true, false, true, false, true, false};
            for (unsigned i = 0; i < 6; ++i)
            {
                f.episode.sourceSequence = f.episode.retentionSourceSequence = 77 + i;
                f.episode.globalNearBlack = nearBlack[i];
                Assert::IsTrue(f.Certificate());
                f.episode.establishedScopeSubtitleRetained = f.Certificate();
                const auto episode = EvaluateNearBlackPresentationEpisode(f.episode);
                Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE), int(episode.state.mode));
                crop.frameSourceSequence = f.episode.sourceSequence;
                crop.nearBlackEpisodeFullRaster = episode.state.mode == NearBlackPresentationMode::FULL_RASTER;
                const auto selected = Evaluate(crop);
                Assert::IsTrue(selected.applyCrop && selected.verticallyTranslated);
                Assert::AreEqual(438, selected.sourceBounds.top);
                Assert::AreEqual(2046, selected.sourceBounds.bottom);
                Assert::AreEqual(1608, selected.sourceBounds.bottom - selected.sourceBounds.top);
                const auto nlsSource = ResolveNlsPresentationSourceGeometry(true, true,
                    f.episode.currentObservation.left, f.episode.currentObservation.top,
                    f.episode.currentObservation.right, f.episode.currentObservation.bottom,
                    selected.applyCrop, selected.sourceBounds.left, selected.sourceBounds.top,
                    selected.sourceBounds.right, selected.sourceBounds.bottom, 3840, 2160);
                Assert::IsTrue(nlsSource.valid);
                Assert::AreEqual(438, nlsSource.top);
                Assert::AreEqual(2046, nlsSource.bottom);
                f.episode.previous = episode.state;
                f.episode.currentTick += 42;
            }
        }

        TEST_METHOD(NlsOwnCropCannotBorrowEstablishedSubtitleTranslation)
        {
            for (double maximumCropPercent : {0.1, 5.0, -1.0,
                std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()})
            {
                DarkSubtitleFixture f;
                f.translation.competingPresentation =
                    NlsCompetesWithEstablishedSubtitleTranslation(true, maximumCropPercent);
                Assert::IsTrue(f.translation.competingPresentation);
                Assert::IsFalse(f.Certificate());
                f.episode.establishedScopeSubtitleRetained = f.Certificate();
                const auto episode = EvaluateNearBlackPresentationEpisode(f.episode);
                Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER), int(episode.state.mode));
            }
            Assert::IsFalse(NlsCompetesWithEstablishedSubtitleTranslation(false, 5.0));
        }

        TEST_METHOD(DarkCaptionRetentionRejectsStaleConflictingAndUncoveredEvidence)
        {
            for (int variant = 0; variant < 23; ++variant)
            {
                DarkSubtitleFixture f;
                switch (variant)
                {
                case 0: f.episode.measurementCurrent = false; break;
                case 1: f.episode.retentionSourceSequence--; break;
                case 2: f.episode.retentionSourceGeneration++; break;
                case 3: f.episode.sceneBoundary = true; break;
                case 4: f.episode.fullRasterAuthorityAvailable = true; break;
                case 5: f.admission.presentationEpoch++; break;
                case 6: f.admission.sourceGeneration++; break;
                case 7: f.admission.available = false; break;
                case 8: f.translation.sourceGeneration++; break;
                case 9: f.translation.base.top++; break;
                case 10: f.translation.competingPresentation = true; break;
                case 11: f.translation.presentation.action = VerticalBarPresentationAction::FIT; break;
                case 12: f.translation.presentation.sourceSequence = f.episode.sourceSequence + 1; break;
                case 13: f.episode.currentTick = 6001; break;
                case 14: f.translation.visibleBounds.bottom = 1955; break; // deeper picture
                case 15: f.translation.visibleBounds.top = 275; break; // opposite-edge intrusion
                case 16: f.episode.currentObservation.top = 275; break;
                case 17: f.episode.currentObservationClassification = ActivePictureClassification::FULL_RASTER_TRUSTED; break;
                case 18: f.episode.previous.mode = NearBlackPresentationMode::FULL_RASTER; break;
                case 19: f.translation.resolvedTranslationPixels = -70; break;
                case 20: f.translation.resolvedTranslationPixels = 0; break;
                case 21: f.translation.visibleBounds = {}; break;
                case 22: f.episode.trustedCropOrigin = ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT; break;
                }
                Assert::IsFalse(f.Certificate());
            }
            DarkSubtitleFixture f;
            f.episode.establishedScopeSubtitleRetained = f.Certificate();
            Assert::IsTrue(EvaluateNearBlackPresentationEpisode(f.episode).state.mode == NearBlackPresentationMode::INACTIVE);
            // A new current intrusion must restore ordinary conservative policy
            // immediately, even while the old subtitle action remains alive.
            f.translation.visibleBounds.bottom = 1980;
            f.episode.establishedScopeSubtitleRetained = f.Certificate();
            Assert::IsFalse(f.episode.establishedScopeSubtitleRetained);
            Assert::IsTrue(EvaluateNearBlackPresentationEpisode(f.episode).state.mode == NearBlackPresentationMode::FULL_RASTER);
        }

        TEST_METHOD(DarkCaptionOnsetUsesFreshPhysicalBarsThroughNormalConfirmation)
        {
            DarkSubtitleFixture f;
            f.translation.presentation = {};
            f.translation.resolvedTranslationPixels = 0;
            f.episode.subtitleInspectionBars = f.episode.trustedCrop;
            f.episode.subtitleInspectionGeneration = f.episode.sourceGeneration;
            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = f.episode.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            VerticalTranslationConfirmationState confirmation;
            for (unsigned sample = 1; sample <= 3; ++sample)
            {
                VerticalTranslationConfirmationInput onset;
                onset.previous = confirmation;
                onset.sourceSequence = f.episode.sourceSequence;
                onset.observed.action = VerticalBarPresentationAction::TRANSLATE;
                onset.observed.translationPixels = 70;
                onset.targetBufferPixels = 10;
                const auto confirmed = ConfirmVerticalTranslation(onset);
                confirmation = confirmed.state;
                Assert::AreEqual(sample < 3, confirmed.pending);
                f.episode.subtitleTranslationConfirmation = confirmation;
                if (!confirmed.pending)
                {
                    f.translation.presentation.action = VerticalBarPresentationAction::TRANSLATE;
                    f.translation.presentation.translationPixels = confirmed.effective.translationPixels;
                    f.translation.presentation.sourceSequence = f.episode.sourceSequence;
                    f.translation.resolvedTranslationPixels = static_cast<int>(confirmed.effective.translationPixels);
                }
                f.episode.retentionSourceSequence = f.episode.sourceSequence;
                f.episode.establishedScopeSubtitleRetained = f.Certificate();
                Assert::IsTrue(f.episode.establishedScopeSubtitleRetained);
                const auto policy = EvaluateNearBlackPresentationEpisode(f.episode);
                Assert::IsTrue(policy.state.mode == NearBlackPresentationMode::INACTIVE);
                crop.frameSourceSequence = f.episode.sourceSequence;
                crop.nearBlackEpisodeFullRaster = policy.state.mode == NearBlackPresentationMode::FULL_RASTER;
                crop.verticalTranslationConfirmationPending = confirmed.pending;
                crop.verticalTranslationActive = !confirmed.pending;
                crop.verticalTranslationPixels = f.translation.resolvedTranslationPixels;
                const auto presented = AdmitCropPresentation(f.admission, crop, Evaluate(crop), f.episode.presentationEpoch);
                Assert::IsFalse(presented.blocked);
                Assert::IsTrue(presented.presentation.applyCrop);
                Assert::AreEqual(1608, presented.presentation.sourceBounds.bottom - presented.presentation.sourceBounds.top);
                f.admission = presented.state;
                f.episode.previous = policy.state;
                ++f.episode.sourceSequence;
                f.episode.currentTick += 42;
            }
        }

        TEST_METHOD(DarkConfirmedCaptionKeepsScopeDuringZeroAndPartialEngage)
        {
            DarkSubtitleFixture f;
            auto crop = TrustedScopeCrop();
            crop.geometry = crop.verticalTranslationBase = f.episode.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = f.episode.sourceGeneration;
            crop.verticalTranslationSourceGeneration = f.episode.sourceGeneration;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            VerticalTranslationDrift drift;
            for (int step = 0; step <= 5; ++step)
            {
                f.episode.currentTick = 1000 + step * 100;
                f.episode.retentionSourceSequence = f.episode.sourceSequence;
                const auto applied = drift.Resolve(70, f.episode.currentTick, 500);
                f.translation.engagingTranslation = drift.IsActive();
                VerticalBarPresentationResolutionInput resolution;
                resolution.detailedAction = VerticalBarPresentationAction::TRANSLATE;
                resolution.translationPixels = applied;
                resolution.zeroTranslationRetainsTrustedBase = drift.IsActive();
                resolution.authoritativeTop = f.episode.trustedCrop.top;
                resolution.authoritativeBottom = f.episode.trustedCrop.bottom;
                resolution.rasterHeight = 2160;
                const auto routed = ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(resolution));
                f.translation.resolvedTranslationPixels = routed.translationPixels;
                Assert::AreEqual(step == 0, !routed.translationActive);
                f.episode.establishedScopeSubtitleRetained = f.Certificate();
                Assert::IsTrue(f.episode.establishedScopeSubtitleRetained);
                const auto policy = EvaluateNearBlackPresentationEpisode(f.episode);
                Assert::IsTrue(policy.state.mode == NearBlackPresentationMode::INACTIVE);
                crop.frameSourceSequence = f.episode.sourceSequence;
                crop.nearBlackEpisodeFullRaster = policy.state.mode == NearBlackPresentationMode::FULL_RASTER;
                crop.verticalTranslationEngageBaseRetentionActive = drift.IsActive() && !routed.translationActive;
                crop.verticalTranslationActive = routed.translationActive;
                crop.verticalTranslationPixels = routed.translationPixels;
                const auto presented = AdmitCropPresentation(f.admission, crop, Evaluate(crop), f.episode.presentationEpoch);
                Assert::IsFalse(presented.blocked);
                Assert::IsTrue(presented.presentation.applyCrop);
                Assert::AreEqual(1608, presented.presentation.sourceBounds.bottom - presented.presentation.sourceBounds.top);
                Assert::AreEqual(1884 + routed.translationPixels, presented.presentation.sourceBounds.bottom);
                f.admission = presented.state;
                f.episode.previous = policy.state;
                ++f.episode.sourceSequence;
            }
            Assert::IsFalse(drift.IsActive());
            // The certificate never renews the owner's timestamp. A held target
            // cannot turn expiration or a caption's disappearance into a lease.
            f.episode.retentionSourceSequence = f.episode.sourceSequence;
            f.translation.engagingTranslation = true;
            f.translation.resolvedTranslationPixels = 14;
            f.episode.currentTick = 6001;
            Assert::IsFalse(f.Certificate());
            f.episode.currentTick = 1100;
            f.episode.boundedVisibleContentOutsideCrop = false;
            Assert::IsFalse(f.Certificate());
            f.episode.boundedVisibleContentOutsideCrop = true;
            f.translation.presentation = {}; // Release drift has no requested owner.
            Assert::IsFalse(f.Certificate());
            f.translation.presentation.action = VerticalBarPresentationAction::TRANSLATE;
            f.translation.presentation.translationPixels = 70;
            f.translation.presentation.sourceSequence = f.episode.sourceSequence;
            f.translation.engagingTranslation = false;
            Assert::IsFalse(f.Certificate()); // Partial reach alone cannot cover this witness.
        }
        TEST_METHOD(DarkPendingCaptionCannotUseHeldOrConflictingPhysicalBars)
        {
            for (int variant = 0; variant < 8; ++variant)
            {
                DarkSubtitleFixture f;
                f.translation.presentation = {};
                f.translation.resolvedTranslationPixels = 0;
                f.episode.subtitleInspectionBars = f.episode.trustedCrop;
                f.episode.subtitleInspectionGeneration = f.episode.sourceGeneration;
                f.episode.subtitleTranslationConfirmation = {70, 1, f.episode.sourceSequence};
                Assert::IsTrue(f.Certificate());
                switch (variant)
                {
                case 0: f.episode.subtitleInspectionBars = {}; break;
                case 1: f.episode.subtitleInspectionBars.top++; break;
                case 2: f.episode.subtitleInspectionGeneration++; break;
                case 3: f.episode.subtitleTranslationConfirmation.lastObservedSourceSequence--; break;
                case 4: f.episode.subtitleTranslationConfirmation.confirmations = 0; break;
                case 5: f.episode.subtitleTranslationConfirmation.confirmations = 3; break;
                case 6: f.episode.subtitleTranslationConfirmation.candidateTranslationPixels = 40; break;
                case 7: f.translation.presentation.action = VerticalBarPresentationAction::FIT; break;
                }
                f.episode.establishedScopeSubtitleRetained = f.Certificate();
                Assert::IsFalse(f.episode.establishedScopeSubtitleRetained);
                Assert::IsTrue(EvaluateNearBlackPresentationEpisode(f.episode).state.mode == NearBlackPresentationMode::FULL_RASTER);
            }
        }
        TEST_METHOD(DarkCaptionRetentionHandlesUpperBarWithoutExtendingItsLease)
        {
            DarkSubtitleFixture f;
            f.translation.presentation.translationPixels = -70;
            f.translation.resolvedTranslationPixels = -70;
            f.translation.visibleBounds = f.episode.trustedCrop;
            f.translation.visibleBounds.top = 223;
            f.episode.currentObservation = f.translation.visibleBounds;
            Assert::IsTrue(f.Certificate());
            f.translation.visibleBounds.bottom++;
            Assert::IsFalse(f.Certificate());
            f.translation.visibleBounds.bottom--;
            f.episode.currentTick = 6001;
            Assert::IsFalse(f.Certificate());
        }
        TEST_METHOD(ScopeOnsetRetainsPresentationThroughConfirmationAndEngage)
        {
            auto episode = ReaffirmedRetainedScope();
            episode.retentionSafe = episode.retentionExcludedBandsPixelSafe = false;
            episode.boundedVisibleContentOutsideCrop = true;
            episode.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
            episode.subtitleInspectionBars = episode.trustedCrop;
            episode.subtitleInspectionGeneration = episode.sourceGeneration;
            auto crop = TrustedScopeCrop();
            crop.geometry = episode.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = episode.sourceGeneration;
            auto admission = AdmitCropPresentation({}, crop, Evaluate(crop), episode.presentationEpoch).state;
            Assert::IsTrue(admission.available);
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            crop.verticalTranslationBase = crop.geometry;
            crop.verticalTranslationSourceGeneration = episode.sourceGeneration;
            VerticalTranslationConfirmationState confirmation;
            for (unsigned sample = 1; sample <= 3; ++sample)
            {
                ++episode.sourceSequence;
                VerticalTranslationConfirmationInput onset;
                onset.previous = confirmation;
                onset.sourceSequence = episode.sourceSequence;
                onset.observed.action = VerticalBarPresentationAction::TRANSLATE;
                onset.observed.translationPixels = 62;
                onset.targetBufferPixels = 10;
                const auto confirmed = ConfirmVerticalTranslation(onset);
                confirmation = confirmed.state;
                Assert::AreEqual(sample < 3, confirmed.pending);
                episode.subtitleTranslationConfirmation = confirmation;
                if (!confirmed.pending)
                {
                    episode.subtitlePresentation.action = VerticalBarPresentationAction::TRANSLATE;
                    episode.subtitlePresentation.translationPixels = confirmed.effective.translationPixels;
                    episode.subtitlePresentation.sourceSequence = episode.sourceSequence;
                }
                const auto policy = EvaluateNearBlackPresentationEpisode(episode);
                Assert::IsFalse(policy.changedToFullRaster);
                Assert::IsTrue(policy.state.mode == NearBlackPresentationMode::INACTIVE);
                episode.previous = policy.state;
                crop.frameSourceSequence = episode.sourceSequence;
                crop.nearBlackEpisodeFullRaster = policy.state.mode == NearBlackPresentationMode::FULL_RASTER;
                crop.verticalTranslationConfirmationPending = confirmed.pending;
                crop.verticalTranslationEngageBaseRetentionActive = !confirmed.pending;
                const auto presented = AdmitCropPresentation(admission, crop, Evaluate(crop), episode.presentationEpoch);
                Assert::IsFalse(presented.blocked);
                Assert::IsTrue(presented.presentation.applyCrop);
                Assert::AreEqual(episode.trustedCrop.top, presented.presentation.sourceBounds.top);
                Assert::AreEqual(episode.trustedCrop.bottom, presented.presentation.sourceBounds.bottom);
                admission = presented.state;
            }
            // If evidence disappears rather than confirming, the ordinary crop
            // policy must stop retaining unsafe pixels; this is not a new lease.
            crop.verticalTranslationEngageBaseRetentionActive = false;
            Assert::IsFalse(Evaluate(crop).applyCrop);
        }

        TEST_METHOD(ScopeOnsetCannotBypassFullRasterOrStaleConflictingEvidence)
        {
            auto input = ReaffirmedRetainedScope();
            ++input.sourceSequence;
            input.retentionSafe = input.retentionExcludedBandsPixelSafe = false;
            input.boundedVisibleContentOutsideCrop = true;
            input.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
            input.subtitleInspectionBars = input.trustedCrop;
            input.subtitleInspectionGeneration = input.sourceGeneration;
            input.subtitleTranslationConfirmation = {62, 1, input.sourceSequence};
            Assert::IsTrue(EvaluateNearBlackPresentationEpisode(input).ended);
            auto reject = [&](NearBlackPresentationEpisodeInput bad) {
                const auto decision = EvaluateNearBlackPresentationEpisode(bad);
                Assert::IsTrue(decision.state.mode == NearBlackPresentationMode::FULL_RASTER);
            };
            auto bad = input; bad.subtitleInspectionGeneration++; reject(bad);
            bad = input; bad.subtitleInspectionBars = {}; reject(bad);
            bad = input; bad.subtitleInspectionBars.top++; reject(bad);
            bad = input; bad.subtitleTranslationConfirmation.lastObservedSourceSequence--; reject(bad);
            bad = input; bad.subtitleTranslationConfirmation.confirmations = 0; reject(bad);
            bad = input; bad.subtitleTranslationConfirmation.confirmations = 3; reject(bad);
            bad = input; bad.subtitleTranslationConfirmation.candidateTranslationPixels = 0; reject(bad);
            bad = input; bad.subtitlePresentation.action = VerticalBarPresentationAction::FIT; reject(bad);
            bad = input; bad.currentObservationClassification = ActivePictureClassification::FULL_RASTER_TRUSTED; reject(bad);
            bad = input; bad.currentObservationClassification = ActivePictureClassification::UNAVAILABLE; reject(bad);
            bad = input; bad.previous.entryTrustedCrop.top++; reject(bad);
            bad = input; bad.previous.mode = NearBlackPresentationMode::FULL_RASTER; reject(bad);
            bad = input; bad.globalNearBlack = true; reject(bad);
            // A full-raster authority or epoch/scene reset must never be
            // overridden by an old pending translation (normal policy owns it).
            bad = input; bad.fullRasterAuthorityAvailable = true;
            Assert::IsFalse(EvaluateNearBlackPresentationEpisode(bad).reason ==
                std::string("fresh physical bars preserved established scope during bounded confirmation"));
            bad = input; bad.presentationEpoch++;
            Assert::IsFalse(EvaluateNearBlackPresentationEpisode(bad).reason ==
                std::string("fresh physical bars preserved established scope during bounded confirmation"));
            bad = input; bad.sceneBoundary = true;
            Assert::IsFalse(EvaluateNearBlackPresentationEpisode(bad).reason ==
                std::string("fresh physical bars preserved established scope during bounded confirmation"));
        }

        TEST_METHOD(SharedCaptionReproofReleasesEpisodeOnlyWithFreshConfirmedTranslation)
        {
            auto input=ReaffirmedRetainedScope(true);
            ++input.sourceSequence;
            input.retentionSafe=input.retentionExcludedBandsPixelSafe=false;
            input.boundedVisibleContentOutsideCrop=true;
            input.currentObservationClassification=ActivePictureClassification::PROVISIONAL;
            input.subtitleInspectionBars=input.trustedCrop;
            input.subtitleInspectionGeneration=input.sourceGeneration;
            input.subtitlePresentation.action=VerticalBarPresentationAction::TRANSLATE;
            input.subtitlePresentation.translationPixels=32;
            input.subtitlePresentation.sourceSequence=input.sourceSequence;
            Assert::IsTrue(EvaluateNearBlackPresentationEpisode(input).ended);
            auto reject=[&](NearBlackPresentationEpisodeInput bad) {
                Assert::IsFalse(EvaluateNearBlackPresentationEpisode(bad).state.mode==NearBlackPresentationMode::INACTIVE);
            };
            auto bad=input;bad.subtitleInspectionGeneration++;reject(bad);
            bad=input;bad.subtitlePresentation.sourceSequence--;reject(bad);
            bad=input;bad.subtitlePresentation.action=VerticalBarPresentationAction::FIT;reject(bad);
            bad=input;bad.subtitleInspectionBars.bottom+=30;reject(bad);
            bad=input;bad.subtitleInspectionBars={};reject(bad);
            bad=input;bad.globalNearBlack=true;reject(bad);
            bad=input;bad.measurementCurrent=false;reject(bad);
            bad=input;bad.currentObservationClassification=ActivePictureClassification::UNAVAILABLE;reject(bad);
        }






		TEST_METHOD(KnownFullRasterCooldownCutMustRevokeDespiteSuppressedBoundary)
		{
			FullRasterReplay replay;
			replay.Frame(100, 128);
			Assert::IsTrue(replay.retention.available && replay.transition.publish);
			replay.Frame(101, 112);
			replay.Frame(102, 100);
			replay.Frame(103, 96);
			Assert::IsTrue(replay.scene.safeBoundary && replay.darknessExemption && replay.retention.available);
			replay.Frame(104, 256);
			Assert::IsTrue(replay.geometryAvailable && replay.fullAuthority);
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::INACTIVE),
				static_cast<int>(replay.episode.mode)); // Bright full-frame cut still has current authority.
			replay.Frame(105, 80, true);
			Assert::IsFalse(replay.scene.safeBoundary); // Scene notification cooldown.
			Assert::IsTrue(replay.scene.nearBlackEntry && replay.scene.hardCutCandidate);
			Assert::AreEqual(static_cast<int>(ActivePictureClassification::UNAVAILABLE),
				static_cast<int>(replay.raw.classification));
			Assert::IsFalse(replay.retention.available,
				L"A suppressed cut notification must not preserve full-frame authority over dark scope.");
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(replay.episode.mode));
			for (uint64_t sequence = 106; sequence < 180; ++sequence)
			{
				replay.Frame(sequence, 80, true);
				Assert::IsFalse(replay.retention.available);
				Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::FULL_RASTER),
					static_cast<int>(replay.episode.mode));
			}
		}

		TEST_METHOD(KnownFullRasterFreshReaffirmationsMustRecoverWithoutNewGeometryPublication)
		{
			FullRasterReplay replay;
			replay.Frame(100, 128);
			Assert::IsTrue(replay.retention.available && replay.transition.publish);
			replay.Frame(101, 120, true);
			Assert::AreEqual(static_cast<int>(ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(replay.raw.classification));
			Assert::IsFalse(replay.retention.available || replay.transition.publish);
			replay.Frame(102, 128);
			Assert::IsFalse(replay.transition.publish || replay.retention.available);
			replay.Frame(103, 128);
			Assert::AreEqual(static_cast<int>(ActivePictureClassification::FULL_RASTER_TRUSTED),
				static_cast<int>(replay.raw.classification));
			Assert::IsFalse(replay.transition.publish); // Same geometry does not republish.
			Assert::AreEqual(uint64_t{100}, replay.commitSequence);
			Assert::IsTrue(replay.retention.available,
				L"Fresh affirmative full-frame pixels must recover the existing committed geometry.");
			replay.Frame(104, 112);
			replay.Frame(105, 100);
			replay.Frame(106, 96);
			Assert::IsTrue(replay.darknessExemption && replay.retention.available);
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::INACTIVE),
				static_cast<int>(replay.episode.mode));
		}


		TEST_METHOD(KnownFullRasterReaffirmationNeedsCurrentNonDarkExactCommittedContext)
		{
			for (int fault = 0; fault < 14; ++fault)
			{
				auto input = ReaffirmingCommittedFullRaster();
				for (uint64_t sequence : {102ull, 103ull})
				{
					input.sourceSequence = sequence;
					switch (fault)
					{
					case 0: input.previous.lastCommittedSequence = 0; break; // Unknown startup.
					case 1: input.measurementCurrent = false; break;
					case 2: input.nearBlackEvaluated = false; break;
					case 3: input.globalNearBlack = true; break;
					case 4: input.rawClassification = ActivePictureClassification::UNAVAILABLE; break;
					case 5: input.rawClassification = ActivePictureClassification::PROVISIONAL; break;
					case 6: input.rawBounds.top = 20; break;
					case 7: input.committedFullAvailable = false; break;
					case 8: input.committedSourceSequence = 99; break;
					case 9: input.committedBounds.right = 1920; break;
					case 10: input.sourceGeneration = 8; break;
					case 11: input.presentationEpoch = 4; break;
					case 12: input.analysisValid = false; break;
					case 13: input.cadenceRepeat = true; break;
					}
					input.previous = UpdateKnownFullRasterRetention(input);
					Assert::IsFalse(input.previous.available);
				}
			}
		}

		TEST_METHOD(KnownFullRasterSparseMeasurementsCanReaffirmButDuplicatesCannot)
		{
			auto input = ReaffirmingCommittedFullRaster();
			input.previous = UpdateKnownFullRasterRetention(input); // Fresh measurement 102.
			Assert::IsFalse(input.previous.available);
			Assert::AreEqual(uint8_t{1}, input.previous.reaffirmationSamples);
			input.cadenceRepeat = true;
			input.previous = UpdateKnownFullRasterRetention(input); // Same source frame.
			Assert::IsFalse(input.previous.available);
			Assert::AreEqual(uint8_t{1}, input.previous.reaffirmationSamples);
			input.sourceSequence = 103;
			input.cadenceRepeat = false;
			input.measurementCurrent = false; // Normal sparse acquisition cadence.
			input.previous = UpdateKnownFullRasterRetention(input);
			Assert::IsFalse(input.previous.available);
			Assert::AreEqual(uint8_t{1}, input.previous.reaffirmationSamples);
			input.sourceSequence = 104;
			input.measurementCurrent = true;
			input.previous = UpdateKnownFullRasterRetention(input);
			Assert::IsTrue(input.previous.available);
			Assert::AreEqual(uint64_t{100}, input.previous.lastCommittedSequence);
		}

		TEST_METHOD(KnownFullRasterMeasuredAmbiguityAndCutsRestartReaffirmation)
		{
			for (int interruption = 0; interruption < 6; ++interruption)
			{
				auto input = ReaffirmingCommittedFullRaster();
				input.previous = UpdateKnownFullRasterRetention(input);
				Assert::AreEqual(uint8_t{1}, input.previous.reaffirmationSamples);
				auto interrupted = input;
				interrupted.sourceSequence = 103;
				if (interruption == 0) interrupted.rawClassification = ActivePictureClassification::UNAVAILABLE;
				if (interruption == 1) interrupted.rawClassification = ActivePictureClassification::PROVISIONAL;
				if (interruption == 2) interrupted.globalNearBlack = true;
				if (interruption == 3) interrupted.independentCutEvidence = true;
				if (interruption == 4) interrupted.analysisValid = false;
				if (interruption == 5) interrupted.sceneBoundary = true;
				input.previous = UpdateKnownFullRasterRetention(interrupted);
				Assert::IsFalse(input.previous.available);
				Assert::AreEqual(uint8_t{0}, input.previous.reaffirmationSamples);
				input.sourceSequence = 104;
				input.previous = UpdateKnownFullRasterRetention(input);
				Assert::IsFalse(input.previous.available);
				input.sourceSequence = 105;
				Assert::IsTrue(UpdateKnownFullRasterRetention(input).available);
			}
		}

		TEST_METHOD(KnownFullRasterPendingCutCannotAccumulateReaffirmationDuringCooldown)
		{
			auto input = ReaffirmingCommittedFullRaster();
			SceneDetectorResult scene;
			scene.differenceEvaluated = true;
			for (uint64_t sequence = 102; sequence < 109; ++sequence)
			{
				input.sourceSequence = scene.sourceSequence = sequence;
				scene.hardCutCandidate = (sequence % 2) != 0;
				scene.hardCutConfirmed = !scene.hardCutCandidate;
				Assert::IsFalse(scene.safeBoundary); // Notification remains debounced.
				input.previous = UpdateKnownFullRasterRetentionForScene(input, scene, false);
				Assert::IsFalse(input.previous.available);
				Assert::AreEqual(uint8_t{0}, input.previous.reaffirmationSamples);
			}
			scene.hardCutCandidate = scene.hardCutConfirmed = false;
			input.sourceSequence = scene.sourceSequence = 109;
			input.previous = UpdateKnownFullRasterRetentionForScene(input, scene, false);
			Assert::IsFalse(input.previous.available);
			input.sourceSequence = scene.sourceSequence = 110;
			Assert::IsTrue(UpdateKnownFullRasterRetentionForScene(input, scene, false).available);
		}

		TEST_METHOD(KnownFullRasterActualCommitDuringCutMustWaitForFreshReaffirmation)
		{
			auto input = CommittedFullRasterRetention(100);
			input.nearBlackEvaluated = true;
			SceneDetectorResult scene;
			scene.sourceSequence = 100;
			scene.hardCutCandidate = true;
			input.previous = UpdateKnownFullRasterRetentionForScene(input, scene, false);
			Assert::IsFalse(input.previous.available);
			Assert::AreEqual(uint64_t{100}, input.previous.lastCommittedSequence);
			input.sourceSequence = scene.sourceSequence = 101;
			input.previous = UpdateKnownFullRasterRetentionForScene(input, scene, false);
			Assert::IsFalse(input.previous.available);
			scene.hardCutCandidate = false;
			input.sourceSequence = scene.sourceSequence = 102;
			input.previous = UpdateKnownFullRasterRetentionForScene(input, scene, false);
			Assert::IsFalse(input.previous.available);
			input.sourceSequence = scene.sourceSequence = 103;
			Assert::IsTrue(UpdateKnownFullRasterRetentionForScene(input, scene, false).available);
		}

		TEST_METHOD(DarknessBoundaryGradualFadePreservesFullRasterThroughRealPolicyChain)
		{
			SceneDetector detector;
			auto retention = CommittedFullRasterRetention(100);
			retention.previous = UpdateKnownFullRasterRetention(retention);
			NearBlackPresentationEpisodeState episode;
			const int levels[] = {160, 144, 128, 112, 96};
			bool sawDarknessBoundary = false;
			for (size_t index = 0; index < _countof(levels); ++index)
			{
				const uint64_t sequence = 101 + index;
				std::vector<uint16_t> pixels(64 * 36, static_cast<uint16_t>(levels[index] << 6));
				const auto scene = detector.Analyze({pixels.data(), 64, 36, 128, sequence,
					static_cast<int64_t>(sequence * 417083), 7, 417083, true});
				retention.sourceSequence = sequence;
				retention.rawClassification = ActivePictureClassification::UNAVAILABLE;
				KnownFullRasterDarknessBoundaryInput boundary;
				boundary.retention = retention;
				boundary.safeBoundary = scene.safeBoundary;
				boundary.nearBlackEntry = scene.nearBlackEntry;
				boundary.differenceEvaluated = scene.differenceEvaluated;
				boundary.hardCutCandidate = scene.hardCutCandidate;
				boundary.hardCutConfirmed = scene.hardCutConfirmed;
				const bool keep = CanRetainKnownFullRasterAtDarknessBoundary(boundary);
				if (scene.safeBoundary)
				{
					sawDarknessBoundary = true;
					Assert::IsTrue(scene.nearBlackEntry);
					Assert::IsFalse(scene.hardCutCandidate || scene.hardCutConfirmed);
					Assert::IsTrue(keep);
					SceneInput policy;
					policy.geometryAvailable = policy.geometryIsCurrentGeneration = policy.latestEvidenceIsCurrent = true;
					policy.geometryClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
					policy.latestClassification = ActivePictureClassification::UNAVAILABLE;
					Assert::AreEqual(static_cast<int>(ScenePresentationAction::WITHDRAW),
						static_cast<int>(EvaluateSceneBoundary(policy).action));
					policy.knownFullRasterDarknessRetention = keep;
					Assert::AreEqual(static_cast<int>(ScenePresentationAction::KEEP_CURRENT),
						static_cast<int>(EvaluateSceneBoundary(policy).action));
				}
				retention.sceneBoundary = scene.safeBoundary && !keep;
				retention.previous = UpdateKnownFullRasterRetention(retention);
				NearBlackPresentationEpisodeInput input;
				input.previous = episode;
				input.sourceGeneration = 7;
				input.sourceSequence = sequence;
				input.presentationEpoch = 3;
				input.measurementCurrent = input.nearBlackEvaluated = true;
				input.globalNearBlack = levels[index] <= 96;
				input.sceneBoundary = scene.safeBoundary && !keep;
				input.fullRasterAuthorityAvailable = true;
				input.knownFullRasterRetained = retention.previous.available;
				const auto result = EvaluateNearBlackPresentationEpisode(input);
				episode = result.state;
				Assert::IsTrue(retention.previous.available);
				Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::INACTIVE), static_cast<int>(episode.mode));
			}
			Assert::IsTrue(sawDarknessBoundary);
		}

		TEST_METHOD(DarknessBoundaryAbruptDarkCutStillWithdrawsThroughRealPolicyChain)
		{
			for (bool rawBars : {false, true})
			{
				SceneDetector detector;
				auto retention = CommittedFullRasterRetention(100);
				retention.previous = UpdateKnownFullRasterRetention(retention);
				std::vector<uint16_t> bright(64 * 36, static_cast<uint16_t>(512 << 6));
				std::vector<uint16_t> dark(64 * 36, static_cast<uint16_t>(80 << 6));
				detector.Analyze({bright.data(),64,36,128,100,41708300,7,417083,true});
				const auto scene = detector.Analyze({dark.data(),64,36,128,101,42125383,7,417083,true});
				Assert::IsTrue(scene.safeBoundary && scene.nearBlackEntry && scene.hardCutCandidate);
				retention.sourceSequence = 101;
				retention.rawClassification = rawBars ? ActivePictureClassification::BAR_CROP_TRUSTED :
					ActivePictureClassification::UNAVAILABLE;
				KnownFullRasterDarknessBoundaryInput boundary;
				boundary.retention = retention;
				boundary.safeBoundary = scene.safeBoundary;
				boundary.nearBlackEntry = scene.nearBlackEntry;
				boundary.differenceEvaluated = scene.differenceEvaluated;
				boundary.hardCutCandidate = scene.hardCutCandidate;
				boundary.hardCutConfirmed = scene.hardCutConfirmed;
				const bool keep = CanRetainKnownFullRasterAtDarknessBoundary(boundary);
				Assert::IsFalse(keep);
				SceneInput policy;
				policy.geometryAvailable = policy.geometryIsCurrentGeneration = policy.latestEvidenceIsCurrent = true;
				policy.geometryClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
				policy.latestClassification = retention.rawClassification;
				policy.knownFullRasterDarknessRetention = keep;
				const auto sceneDecision = EvaluateSceneBoundary(policy);
				Assert::AreEqual(static_cast<int>(ScenePresentationAction::WITHDRAW), static_cast<int>(sceneDecision.action));
				retention.committedFullAvailable = false;
				retention.sceneBoundary = scene.safeBoundary && !keep;
				retention.previous = UpdateKnownFullRasterRetention(retention);
				Assert::IsFalse(retention.previous.available);
				NearBlackPresentationEpisodeInput input;
				input.sourceGeneration = 7;
				input.sourceSequence = 101;
				input.presentationEpoch = 3;
				input.measurementCurrent = input.nearBlackEvaluated = input.globalNearBlack = true;
				input.sceneBoundary = scene.safeBoundary && !keep;
				input.knownFullRasterRetained = retention.previous.available;
				input.fullRasterAuthorityAvailable = false;
				Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::FULL_RASTER),
					static_cast<int>(EvaluateNearBlackPresentationEpisode(input).state.mode));
			}
		}

		TEST_METHOD(DarknessBoundarySimultaneousConfirmedCutIsNotExempt)
		{
			SceneDetector detector;
			SceneDetectorResult scene;
			uint64_t sequence = 99;
			for (int level : {256, 100, 90})
			{
				std::vector<uint16_t> pixels(64 * 36, static_cast<uint16_t>(level << 6));
				++sequence;
				scene = detector.Analyze({pixels.data(),64,36,128,sequence,
					static_cast<int64_t>(sequence * 417083),7,417083,true});
			}
			Assert::IsTrue(scene.safeBoundary && scene.nearBlackEntry && scene.hardCutConfirmed);
			auto retention = CommittedFullRasterRetention(100);
			retention.previous = UpdateKnownFullRasterRetention(retention);
			retention.sourceSequence = sequence;
			retention.rawClassification = ActivePictureClassification::UNAVAILABLE;
			KnownFullRasterDarknessBoundaryInput boundary;
			boundary.retention = retention;
			boundary.safeBoundary = scene.safeBoundary;
			boundary.nearBlackEntry = scene.nearBlackEntry;
				boundary.differenceEvaluated = scene.differenceEvaluated;
			boundary.hardCutCandidate = scene.hardCutCandidate;
			boundary.hardCutConfirmed = scene.hardCutConfirmed;
			Assert::IsFalse(CanRetainKnownFullRasterAtDarknessBoundary(boundary));
		}

		TEST_METHOD(DarknessBoundaryRequiresPriorCurrentExactFullAuthority)
		{
			KnownFullRasterDarknessBoundaryInput valid;
			valid.retention = CommittedFullRasterRetention(100);
			valid.retention.previous = UpdateKnownFullRasterRetention(valid.retention);
			valid.retention.sourceSequence = 101;
			valid.retention.rawClassification = ActivePictureClassification::UNAVAILABLE;
			valid.safeBoundary = valid.nearBlackEntry = valid.differenceEvaluated = true;
			Assert::IsTrue(CanRetainKnownFullRasterAtDarknessBoundary(valid));
			for (int invalid = 0; invalid < 20; ++invalid)
			{
				auto input = valid;
				switch (invalid)
				{
				case 0: input.retention.previous = {}; break; // Unknown startup.
				case 1: ++input.retention.sourceGeneration; break;
				case 2: ++input.retention.presentationEpoch; break;
				case 3: ++input.retention.frameWidth; break;
				case 4: input.retention.analysisValid = false; break;
				case 5: input.retention.measurementCurrent = false; break;
				case 6: input.retention.cadenceRepeat = true; break;
				case 7: input.retention.rawClassification = ActivePictureClassification::BAR_CROP_TRUSTED; break;
				case 8: input.retention.committedFullAvailable = false; break;
				case 9: input.retention.committedBounds.top = 10; break;
				case 10: input.retention.committedBounds.trustedBarAxes = ActivePictureBounds::BarAxes::TOP_BOTTOM; break;
				case 11: ++input.retention.committedSourceGeneration; break;
				case 12: ++input.retention.committedPresentationEpoch; break;
				case 13: input.retention.sourceSequence = 100; break;
				case 14: input.retention.committedSourceSequence = 99; break;
				case 15: input.safeBoundary = false; break;
				case 16: input.nearBlackEntry = false; break;
				case 17: input.hardCutCandidate = true; break;
				case 18: input.hardCutConfirmed = true; break;
				case 19: input.differenceEvaluated = false; break;
				}
				Assert::IsFalse(CanRetainKnownFullRasterAtDarknessBoundary(input));
			}
		}


		TEST_METHOD(DarknessBoundaryDetectorResetCannotPreserveAnOldLease)
		{
			SceneDetector detector;
			auto retention = CommittedFullRasterRetention(100);
			retention.previous = UpdateKnownFullRasterRetention(retention);
			std::vector<uint16_t> bright(64 * 36, static_cast<uint16_t>(160 << 6));
			detector.Analyze({bright.data(),64,36,128,100,41708300,7,417083,true});
			detector.Reset(7); // Scene history can reset without capture generation changing.
			std::vector<uint16_t> dark(64 * 36, static_cast<uint16_t>(90 << 6));
			const auto scene = detector.Analyze({dark.data(),64,36,128,101,42125383,7,417083,true});
			Assert::IsTrue(scene.safeBoundary && scene.nearBlackEntry);
			Assert::IsFalse(scene.differenceEvaluated);
			retention.sourceSequence = 101;
			retention.rawClassification = ActivePictureClassification::UNAVAILABLE;
			KnownFullRasterDarknessBoundaryInput boundary;
			boundary.retention = retention;
			boundary.safeBoundary = scene.safeBoundary;
			boundary.nearBlackEntry = scene.nearBlackEntry;
			boundary.differenceEvaluated = scene.differenceEvaluated;
			boundary.hardCutCandidate = scene.hardCutCandidate;
			boundary.hardCutConfirmed = scene.hardCutConfirmed;
			Assert::IsFalse(CanRetainKnownFullRasterAtDarknessBoundary(boundary));
		}

		TEST_METHOD(DarknessBoundarySceneRoutingCannotKeepCroppedOrStaleGeometry)
		{
			SceneInput input;
			input.knownFullRasterDarknessRetention = true;
			input.geometryAvailable = input.geometryIsCurrentGeneration = input.latestEvidenceIsCurrent = true;
			input.geometryClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
			input.latestClassification = ActivePictureClassification::UNAVAILABLE;
			Assert::AreEqual(static_cast<int>(ScenePresentationAction::KEEP_CURRENT),
				static_cast<int>(EvaluateSceneBoundary(input).action));
			input.geometryIsCurrentGeneration = false;
			Assert::AreEqual(static_cast<int>(ScenePresentationAction::WITHDRAW),
				static_cast<int>(EvaluateSceneBoundary(input).action));
			input.geometryIsCurrentGeneration = true;
			input.latestClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			Assert::AreEqual(static_cast<int>(ScenePresentationAction::WITHDRAW),
				static_cast<int>(EvaluateSceneBoundary(input).action));
		}

		TEST_METHOD(KnownFullRasterRequiresActualCurrentFullCommit)
		{
			for (int invalid = 0; invalid < 11; ++invalid)
			{
				auto input = CommittedFullRasterRetention();
				switch (invalid)
				{
				case 0: input.committedFullAvailable = false; break;
				case 1: input.committedSourceGeneration = 6; break;
				case 2: input.committedPresentationEpoch = 2; break;
				case 3: input.committedSourceSequence = 99; break;
				case 4: input.rawClassification = ActivePictureClassification::PROVISIONAL; break;
				case 5: input.rawClassification = ActivePictureClassification::UNAVAILABLE; break;
				case 6: input.committedBounds.top = 100; break;
				case 7: input.rawBounds.trustedBarAxes = ActivePictureBounds::BarAxes::TOP_BOTTOM; break;
				case 8: input.analysisValid = false; break;
				case 9: input.measurementCurrent = false; break;
				case 10: input.rawBounds.rasterWidth = 1920; break;
				}
				Assert::IsFalse(UpdateKnownFullRasterRetention(input).available);
			}
			Assert::IsTrue(UpdateKnownFullRasterRetention(CommittedFullRasterRetention()).available);
		}

		TEST_METHOD(KnownFullRasterRetainsThroughUnavailableAndSparseMeasurements)
		{
			auto input = CommittedFullRasterRetention();
			input.previous = UpdateKnownFullRasterRetention(input);
			for (uint64_t sequence = 101; sequence < 120; ++sequence)
			{
				input.sourceSequence = sequence;
				input.rawClassification = sequence % 2 ? ActivePictureClassification::UNAVAILABLE :
					ActivePictureClassification::PROVISIONAL;
				input.measurementCurrent = sequence % 3 != 0;
				input.previous = UpdateKnownFullRasterRetention(input);
				Assert::IsTrue(input.previous.available);
				Assert::AreEqual(uint64_t{100}, input.previous.lastCommittedSequence);
			}
		}

		TEST_METHOD(KnownFullRasterTrustedScopeAndPillarboxRevokeUntilNewCommit)
		{
			for (auto axes : { ActivePictureBounds::BarAxes::TOP_BOTTOM, ActivePictureBounds::BarAxes::LEFT_RIGHT })
			{
				auto input = CommittedFullRasterRetention();
				input.previous = UpdateKnownFullRasterRetention(input);
				input.sourceSequence = 101;
				input.rawClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
				input.rawBounds = axes == ActivePictureBounds::BarAxes::TOP_BOTTOM ?
					ActivePictureBounds{0, 272, 3840, 1884, 3840, 2160, 2.38, axes} :
					ActivePictureBounds{480, 0, 3360, 2160, 3840, 2160, 4.0 / 3.0, axes};
				input.previous = UpdateKnownFullRasterRetention(input);
				Assert::IsFalse(input.previous.available);
				for (uint64_t sequence = 102; sequence < 106; ++sequence)
				{
					input.sourceSequence = sequence;
					input.rawClassification = sequence % 2 ? ActivePictureClassification::UNAVAILABLE :
						ActivePictureClassification::PROVISIONAL;
					input.previous = UpdateKnownFullRasterRetention(input);
					Assert::IsFalse(input.previous.available);
				}
				auto fresh = CommittedFullRasterRetention(106);
				fresh.previous = input.previous;
				Assert::IsTrue(UpdateKnownFullRasterRetention(fresh).available);
			}
		}

		TEST_METHOD(KnownFullRasterRepeatCannotCreateOrResurrectAuthority)
		{
			auto input = CommittedFullRasterRetention();
			input.cadenceRepeat = true;
			Assert::IsFalse(UpdateKnownFullRasterRetention(input).available);
			input.cadenceRepeat = false;
			input.previous = UpdateKnownFullRasterRetention(input);
			input.cadenceRepeat = true;
			Assert::IsTrue(UpdateKnownFullRasterRetention(input).available);
			input.analysisValid = false;
			input.previous = UpdateKnownFullRasterRetention(input);
			Assert::IsFalse(input.previous.available);
			input.analysisValid = true;
			input.cadenceRepeat = false;
			Assert::IsFalse(UpdateKnownFullRasterRetention(input).available);
		}

		TEST_METHOD(KnownFullRasterDuplicateContradictionCanWithdrawButCannotRearm)
		{
			for (bool scene : {false, true})
			{
				auto input = CommittedFullRasterRetention();
				input.previous = UpdateKnownFullRasterRetention(input);
				input.cadenceRepeat = true;
				input.sceneBoundary = scene;
				input.rawClassification = scene ? ActivePictureClassification::UNAVAILABLE :
					ActivePictureClassification::BAR_CROP_TRUSTED;
				input.previous = UpdateKnownFullRasterRetention(input);
				Assert::IsFalse(input.previous.available);
				input.cadenceRepeat = input.sceneBoundary = false;
				input.rawClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
				Assert::IsFalse(UpdateKnownFullRasterRetention(input).available);
			}
		}

		TEST_METHOD(KnownFullRasterReorderedFrameRevokesWithoutOldCommitReplay)
		{
			auto input = CommittedFullRasterRetention();
			input.previous = UpdateKnownFullRasterRetention(input);
			input.sourceSequence = 99;
			input.previous = UpdateKnownFullRasterRetention(input);
			Assert::IsFalse(input.previous.available);
			for (uint64_t sequence : {100ull, 101ull})
			{
				input.sourceSequence = sequence;
				input.previous = UpdateKnownFullRasterRetention(input);
				Assert::IsFalse(input.previous.available);
			}
		}

		TEST_METHOD(KnownFullRasterContextChangesRejectStaleCommit)
		{
			for (int change = 0; change < 4; ++change)
			{
				auto input = CommittedFullRasterRetention();
				input.previous = UpdateKnownFullRasterRetention(input);
				input.sourceSequence = 101;
				if (change == 0) ++input.sourceGeneration;
				if (change == 1) ++input.presentationEpoch;
				if (change == 2) input.frameWidth = 1920;
				if (change == 3) input.frameHeight = 1080;
				input.previous = UpdateKnownFullRasterRetention(input);
				Assert::IsFalse(input.previous.available);
				input.sourceSequence = 102;
				input.rawClassification = ActivePictureClassification::UNAVAILABLE;
				Assert::IsFalse(UpdateKnownFullRasterRetention(input).available);
			}
		}

		TEST_METHOD(KnownFullRasterSceneCutNeedsFreshFullEvidence)
		{
			auto input = CommittedFullRasterRetention();
			input.previous = UpdateKnownFullRasterRetention(input);
			input.sourceSequence = 101;
			input.sceneBoundary = true;
			Assert::IsTrue(UpdateKnownFullRasterRetention(input).available);
			input.rawClassification = ActivePictureClassification::UNAVAILABLE;
			input.previous = UpdateKnownFullRasterRetention(input);
			Assert::IsFalse(input.previous.available);
			input.sourceSequence = 102;
			input.sceneBoundary = false;
			input.rawClassification = ActivePictureClassification::FULL_RASTER_TRUSTED;
			Assert::IsFalse(UpdateKnownFullRasterRetention(input).available);
			auto fresh = CommittedFullRasterRetention(103);
			fresh.previous = input.previous;
			fresh.sceneBoundary = true;
			Assert::IsTrue(UpdateKnownFullRasterRetention(fresh).available);
		}

		TEST_METHOD(KnownFullRasterRawBarsOverrideConflictingCommitMetadata)
		{
			auto input = CommittedFullRasterRetention();
			input.rawClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			Assert::IsFalse(UpdateKnownFullRasterRetention(input).available);
		}

		TEST_METHOD(KnownFullRasterDarkEpisodePreservesAuthorityButStartupStillWaits)
		{
			NearBlackPresentationEpisodeInput input;
			input.sourceGeneration = 7;
			input.sourceSequence = 101;
			input.measurementCurrent = input.nearBlackEvaluated = input.globalNearBlack = true;
			input.fullRasterAuthorityAvailable = true;
			const auto startup = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(startup.state.mode));
			input.previous = startup.state;
			input.knownFullRasterRetained = true;
			const auto known = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(known.ended);
			Assert::IsFalse(known.releasedToTrustedCrop);
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::INACTIVE),
				static_cast<int>(known.state.mode));
			Assert::IsTrue(input.globalNearBlack);
			input.previous = known.state;
			Assert::IsFalse(EvaluateNearBlackPresentationEpisode(input).started);
			input.previous = startup.state;
			input.sceneBoundary = true;
			Assert::IsTrue(EvaluateNearBlackPresentationEpisode(input).ended);
		}

		TEST_METHOD(KnownFullRasterEpisodeCannotOverrideTrustedCropOrMissingAuthority)
		{
			NearBlackPresentationEpisodeInput input;
			input.sourceGeneration = 7;
			input.sourceSequence = 101;
			input.measurementCurrent = input.nearBlackEvaluated = input.globalNearBlack = true;
			input.knownFullRasterRetained = true;
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(EvaluateNearBlackPresentationEpisode(input).state.mode));
			input.fullRasterAuthorityAvailable = true;
			input.trustedCropAvailable = true;
			input.trustedCrop = TrustedScopeCrop().geometry;
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::RETAIN_CROP),
				static_cast<int>(EvaluateNearBlackPresentationEpisode(input).state.mode));
		}

		TEST_METHOD(ProvisionalSplitScreenExpansionKeepsBoundedCropThroughSubtitleConfirmation)
		{
			// Replay 12:28:26: horizontal extent expands while lower HDMI content
			// makes the raw geometry provisional. Shared geometry remains trusted.
			for (bool provisional : {true, false})
			{
				PresentationRecoveryState recovery;
				for (uint64_t seq = 3366; seq <= 3590; ++seq)
				{
					Input crop = TrustedScopeCrop();
					crop.geometry = {192,440,3648,1720,3840,2160,2.7,ActivePictureBounds::BarAxes::BOTH};
					crop.frameSourceSequence = seq;
					crop.latestObservationSupportsCrop = false;
					crop.latestObservationIsProvisional = provisional;
					crop.latestObservationClassification = provisional
						? ActivePictureClassification::PROVISIONAL : ActivePictureClassification::BAR_CROP_TRUSTED;
					crop.frameLocalPresentationRetentionEvaluated = true;
					crop.barCropRefinementHorizontalConflict = true;
					crop.currentVisibleBoundsAvailable = true;
					crop.currentVisibleBase = crop.geometry;
					crop.currentVisibleSourceGeneration = crop.outwardExpansionSourceGeneration = 7;
					crop.currentVisibleSourceSequence = seq;
					crop.currentVisibleBounds = crop.geometry;
					crop.currentVisibleBounds.left = 136; crop.currentVisibleBounds.right = 3678;
					crop.currentVisibleBounds.bottom = seq < 3379 ? 2160 : 2020;
					crop.outwardPresentationActive = crop.outwardExpansionAvailable = true;
					crop.outwardExpansion = crop.geometry;
					crop.outwardExpansion.left = 102; crop.outwardExpansion.right = 3712;
					crop.verticalTranslationBase = crop.geometry;
					crop.verticalTranslationSourceGeneration = 7;
					crop.verticalTranslationConfirmationPending = seq < 3379;
					crop.verticalTranslationEngageBaseRetentionActive = seq == 3379;
					crop.verticalTranslationActive = seq > 3379;
					crop.verticalTranslationPixels = seq > 3379 ? 346 : 0;
					PresentationRecoveryInput input;
					input.previous = recovery; input.crop = crop; input.candidate = Evaluate(crop);
					input.measurementCurrent = input.retentionEvaluated = input.nearBlackEvaluated = true;
					input.retentionBounds = crop.geometry;
					input.retentionSourceGeneration = 7; input.retentionSourceSequence = seq;
					input.framesPerSecond = 24.0;
					const auto decision = EvaluatePresentationRecovery(input);
					recovery = decision.state;
					Assert::IsTrue(decision.presentation.applyCrop, L"Provisional geometry must not veto current bounded horizontal pixels");
					Assert::IsTrue(decision.presentation.horizontalExpansionPixelBounded);
					Assert::IsFalse(recovery.active, L"Do not arm a nine-second full-raster recovery during confirmation/engagement");
					Assert::AreEqual(102, decision.presentation.sourceBounds.left);
					Assert::AreEqual(3712, decision.presentation.sourceBounds.right);
					for (int failure = 0; failure < 8; ++failure)
					{
						auto bad = crop;
						switch (failure) {
						case 0: --bad.currentVisibleSourceSequence; break;
						case 1: --bad.currentVisibleSourceGeneration; break;
						case 2: bad.currentVisibleBase.top += 4; break;
						case 3: bad.currentVisibleBoundsAvailable = false; break;
						case 4: bad.currentVisibleBounds.right = 3800; break;
						case 5: bad.latestObservationClassification = ActivePictureClassification::FULL_RASTER_TRUSTED; break;
						case 6: bad.latestObservationIsUnavailable = true; break;
						case 7: --bad.verticalTranslationSourceGeneration; break;
						}
						AssertFullRaster(Evaluate(bad));
					}
				}
			}
		}

		TEST_METHOD(SubtitleInspectionRejectsStaleProofAndDoesNotConsumeOnsetWithoutAuthority)
		{
			SubtitleInspectionInput input;
			input.barAuthorityAvailable=input.measurementCurrent=true;
			input.base=input.measuredBase=TrustedScopeCrop().geometry;
			input.sourceGeneration=7;
			input.retention.analysisValid=input.retention.presentationValid=true;
			input.retention.excludedVerticalBandsPixelSafe=false;
			Assert::IsTrue(UpdateSubtitleInspection(input).forceAnalysis);
			for(int failure=0; failure<5; ++failure) {
				auto bad=input;
				switch(failure) {
				case 0: bad.measurementCurrent=false; break;
				case 1: bad.measuredBase.top+=4; break;
				case 2: bad.retention.analysisValid=false; break;
				case 3: bad.barAuthorityAvailable=false; break;
				case 4: bad.sourceGeneration=0; break;
				}
				const auto blocked=UpdateSubtitleInspection(bad);
				Assert::IsFalse(blocked.forceAnalysis);
				auto retry=input; retry.previous=blocked.state;
				Assert::IsTrue(UpdateSubtitleInspection(retry).forceAnalysis);
			}
			input.previous=UpdateSubtitleInspection(input).state;
			Assert::IsFalse(UpdateSubtitleInspection(input).forceAnalysis);
			input.base.top+=20; input.measuredBase=input.base;
			Assert::IsTrue(UpdateSubtitleInspection(input).forceAnalysis);
			input.translationAlreadyActive=true;
			Assert::IsFalse(UpdateSubtitleInspection(input).forceAnalysis);
		}

		TEST_METHOD(SubtitleOnsetAfterHorizontalContentNeverArmsRecoveryDuringConfirmationOrEngage)
		{
			for (uint64_t onset : {3776ULL,3777ULL,3778ULL})
			{
				const ActivePictureBounds base{192,372,3648,1788,3840,2160,2.44,ActivePictureBounds::BarAxes::BOTH};
				SubtitleInspectionState inspection;
				VerticalTranslationConfirmationState confirmation;
				PresentationRecoveryState recovery;
				bool accepted=false;
				uint64_t acceptedAt=0;
				for (uint64_t seq=onset-12; seq<onset+24; ++seq)
				{
					const bool subtitle=seq>=onset;
					SubtitleInspectionInput probe;
					probe.previous=inspection;
					probe.barAuthorityAvailable=probe.measurementCurrent=true;
					probe.sourceGeneration=1; probe.base=probe.measuredBase=base;
					probe.translationAlreadyActive=accepted;
					probe.retention.analysisValid=probe.retention.presentationValid=true;
					probe.retention.activePicture.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
					probe.retention.excludedBandsPixelSafe=false; // horizontal content precedes subtitle
					probe.retention.excludedVerticalBandsPixelSafe=!subtitle;
					const auto inspected=UpdateSubtitleInspection(probe);
					inspection=inspected.state;
					if(seq==onset) Assert::IsTrue(inspected.forceAnalysis,L"First subtitle frame needs inspection independently of horizontal occupancy");
					const bool scan=inspected.forceAnalysis || seq%3==0;
					if(scan && subtitle) {
						VerticalTranslationConfirmationInput c;
						c.previous=confirmation; c.sourceSequence=seq;
						c.observed.action=VerticalBarPresentationAction::TRANSLATE; c.observed.translationPixels=92;
						c.targetBufferPixels=10; c.acceptedTranslationActive=accepted; c.acceptedTranslationPixels=102;
						const auto d=ConfirmVerticalTranslation(c); confirmation=d.state;
						if(d.newlyAccepted) { accepted=true; acceptedAt=seq; }
					}
					Input crop=TrustedScopeCrop(); crop.geometry=base;
					crop.frameSourceGeneration=crop.geometrySourceGeneration=1; crop.frameSourceSequence=seq;
					crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
					crop.latestObservationSupportsCrop=!subtitle;
					crop.barCropRefinementHorizontalConflict=true;
					crop.currentVisibleBoundsAvailable=true; crop.currentVisibleBase=base;
					crop.currentVisibleSourceGeneration=crop.outwardExpansionSourceGeneration=1;
					crop.currentVisibleSourceSequence=seq; crop.currentVisibleBounds=base;
					crop.currentVisibleBounds.right=3678;
					if(subtitle) { crop.currentVisibleBounds.top=0; crop.currentVisibleBounds.bottom=2160; }
					crop.outwardExpansion=base; crop.outwardExpansion.right=3712;
					crop.outwardExpansionAvailable=crop.outwardPresentationActive=true;
					crop.verticalTranslationBase=base; crop.verticalTranslationSourceGeneration=1;
					crop.verticalTranslationConfirmationPending=confirmation.confirmations!=0;
					crop.verticalTranslationEngageBaseRetentionActive=accepted && seq==acceptedAt;
					crop.verticalTranslationActive=accepted && seq>acceptedAt;
					crop.verticalTranslationPixels=crop.verticalTranslationActive ? 102 : 0;
					PresentationRecoveryInput ri; ri.previous=recovery; ri.crop=crop; ri.candidate=Evaluate(crop);
					ri.measurementCurrent=ri.retentionEvaluated=ri.nearBlackEvaluated=true;
					ri.retentionBounds=base; ri.retentionSourceSequence=seq; ri.retentionSourceGeneration=1;
					const auto d=EvaluatePresentationRecovery(ri); recovery=d.state;
					Assert::IsTrue(d.presentation.applyCrop,L"Scan/confirmation/zero-shift engage must not drop to full raster");
					Assert::IsFalse(d.state.active);
				}
				Assert::IsTrue(accepted);
			}
		}

		TEST_METHOD(HorizontalPixelProofComposesWithCurrentSubtitleOwner)
		{
			Input crop=TrustedScopeCrop();
			crop.geometry={192,372,3648,1788,3840,2160,2.44,ActivePictureBounds::BarAxes::BOTH};
			crop.frameSourceSequence=2200;
			crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.barCropRefinementHorizontalConflict=true;
			crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
			crop.outwardExpansionSourceGeneration=crop.currentVisibleSourceGeneration=7;
			crop.currentVisibleSourceSequence=2200; crop.currentVisibleBase=crop.geometry;
			crop.currentVisibleBoundsAvailable=true;
			crop.currentVisibleBounds=crop.geometry;
			crop.currentVisibleBounds.left=162; crop.currentVisibleBounds.right=3678;
			// Coarse vertical geometry may include the entire bar; the dense owner
			// already handles vertical occupancy independently of horizontal proof.
			crop.currentVisibleBounds.top=0; crop.currentVisibleBounds.bottom=2160;
			crop.outwardExpansion=crop.geometry; crop.outwardExpansion.left=128; crop.outwardExpansion.right=3712;
			AssertFullRaster(Evaluate(crop));
			crop.verticalTranslationBase=crop.geometry; crop.verticalTranslationSourceGeneration=7;
			crop.verticalTranslationConfirmationPending=true;
			Assert::IsTrue(Evaluate(crop).applyCrop);
			crop.verticalTranslationConfirmationPending=false; crop.verticalTranslationActive=true;
			crop.verticalTranslationPixels=102;
			const auto d=Evaluate(crop);
			Assert::IsTrue(d.applyCrop && d.verticallyTranslated && d.horizontalExpansionPixelBounded);
			Assert::AreEqual(128,d.sourceBounds.left); Assert::AreEqual(3712,d.sourceBounds.right);
			Assert::AreEqual(474,d.sourceBounds.top); Assert::AreEqual(1890,d.sourceBounds.bottom);
			for(int failure=0; failure<5; ++failure) {
				auto bad=crop;
				switch(failure) {
				case 0: --bad.currentVisibleSourceSequence; break;
				case 1: --bad.verticalTranslationSourceGeneration; break;
				case 2: bad.verticalTranslationBase.top+=4; break;
				case 3: bad.currentVisibleBounds.right=3730; break;
				case 4: bad.outwardExpansion.bottom+=40; break;
				}
				AssertFullRaster(Evaluate(bad));
			}
		}

		TEST_METHOD(SubtitleOccupancyCannotTurnSafeHorizontalJitterIntoFullRaster)
		{
			const ActivePictureBounds base{192,372,3648,1788,3840,2160,2.44,ActivePictureBounds::BarAxes::BOTH};
			auto observed=base; observed.right+=4; observed.top=0; observed.bottom=2160;
			ActivePicturePresentationRetentionEvidence evidence;
			evidence.analysisValid=evidence.presentationValid=evidence.excludedHorizontalBandsPixelSafe=true;
			evidence.excludedBandsPixelSafe=false; // bottom subtitle is real, side bands are empty
			Assert::IsTrue(IsPixelSafeHorizontalSamplingEnvelope(base,observed,evidence));
			Assert::IsFalse(HasHorizontalCropRefinementConflict(false,false,true,true,true,observed,base));
			// A real measured horizontal extent is never excused by equivalence.
			Assert::IsTrue(HasHorizontalCropRefinementConflict(false,true,true,true,true,observed,base));
			evidence.excludedHorizontalBandsPixelSafe=false;
			Assert::IsFalse(IsPixelSafeHorizontalSamplingEnvelope(base,observed,evidence));
			evidence.excludedHorizontalBandsPixelSafe=true; observed.right+=8;
			Assert::IsFalse(IsPixelSafeHorizontalSamplingEnvelope(base,observed,evidence));
			observed=base; observed.left-=8; observed.right+=8;
			Assert::IsFalse(IsPixelSafeHorizontalSamplingEnvelope(base,observed,evidence));
			observed=base; evidence.presentationValid=false;
			Assert::IsFalse(IsPixelSafeHorizontalSamplingEnvelope(base,observed,evidence));
		}

		TEST_METHOD(ExpansionStripProofRejectsMismatchedBaseCandidateAndSparseContent)
		{
			const auto base=TrustedScopeCrop().geometry;
			auto candidate=base; candidate.top-=36; candidate.bottom+=36;
			ActivePicturePresentationRetentionEvidence evidence;
			evidence.analysisValid=evidence.presentationValid=evidence.expansionStripsAvailable=true;
			evidence.expansionBase=base; evidence.expansionCandidate=candidate;
			evidence.expandingTop.barPixels=36; evidence.expandingTop.blackFraction=0.1;
			evidence.expandingTop.continuity=0.1; evidence.expandingTop.lumaP90=300;
			evidence.expandingBottom=evidence.expandingTop;
			Assert::IsTrue(ConfirmOutwardPictureTransition({},base,candidate,evidence,7,10).broadOpposingPicture);
			for(int failure=0; failure<5; ++failure) {
				auto bad=evidence;
				switch(failure) {
				case 0: bad.expansionBase.top+=4; break;
				case 1: bad.expansionCandidate.bottom+=4; break;
				case 2: bad.expandingBottom.blackFraction=0.98; break;
				case 3: bad.expandingTop.lumaP90=80; break;
				case 4: bad.analysisValid=false; break;
				}
				Assert::IsFalse(ConfirmOutwardPictureTransition({},base,candidate,bad,7,10).broadOpposingPicture);
			}
		}

		TEST_METHOD(LoggedSafeSideBarsCannotCreateFullRasterRecoveryOrHeldWidth)
		{
			const ActivePictureBounds base{176,356,3664,1804,3840,2160,
				3488.0/1448,ActivePictureBounds::BarAxes::BOTH};
			PresentationRecoveryState recoveryState;
			for (uint64_t seq=686;seq<716;++seq)
			{
				ActivePictureEvidence evidence;
				evidence.available=true; evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
				evidence.proposedBounds={188,364,3652,1796,3840,2160,3464.0/1432,ActivePictureBounds::BarAxes::NONE};
				evidence.trustedBounds=evidence.proposedBounds;
				evidence.trustedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
				if (seq==686)
				{
					evidence.trustedBounds.left=0; evidence.trustedBounds.right=3840;
					evidence.trustedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::TOP_BOTTOM;
				}
				ActivePicturePresentationRetentionEvidence retention;
				retention.analysisValid=retention.presentationValid=retention.expansionStripsAvailable=true;
				retention.expansionBase=base; retention.activePicture=evidence;
				retention.excludedBandsPixelSafe=retention.excludedHorizontalBandsPixelSafe=retention.excludedVerticalBandsPixelSafe=true;
				const auto physical=ResolvePresentationObservation(base,evidence,retention);
				const auto& observed=physical.bounds;
				const bool left=observed.left<base.left, right=observed.right>base.right;
				Assert::IsFalse(left || right);
				Input crop=TrustedScopeCrop(); crop.geometry=base;
				crop.frameSourceSequence=seq; crop.latestObservationSupportsCrop=seq!=686;
				crop.latestObservationClassification=evidence.classification;
				crop.barCropRefinementPending=seq==686;
				crop.barCropRefinementHorizontalConflict=HasHorizontalCropRefinementConflict(left,right,true,true,false,observed,base);
				crop.frameLocalPresentationRetentionEvaluated=true;
				if (seq==686)
				{
					// Reproduce the old wiring: full-width confidence placeholders
					// requested a horizontal fit without any outside visible pixels.
					auto old=crop;
					old.barCropRefinementHorizontalConflict=HasHorizontalCropRefinementConflict(
						true,true,true,true,false,evidence.trustedBounds,base);
					AssertFullRaster(Evaluate(old));
				}
				const auto candidate=Evaluate(crop);
				PresentationRecoveryInput recovery;
				recovery.previous=recoveryState; recovery.crop=crop; recovery.candidate=candidate;
				recovery.measurementCurrent=recovery.retentionEvaluated=recovery.nearBlackEvaluated=true;
				recovery.retentionBounds=base; recovery.retentionSourceGeneration=7; recovery.retentionSourceSequence=seq;
				recovery.excludedBandsPixelSafe=true; recovery.observationAvailable=true;
				recovery.observation=evidence.proposedBounds; recovery.observedTrustedCrop=evidence.trustedBounds;
				recovery.observationClassification=evidence.classification;
				const auto final=EvaluatePresentationRecovery(recovery); recoveryState=final.state;
				Assert::IsTrue(final.presentation.applyCrop);
				Assert::IsFalse(final.started || final.state.active || candidate.outwardExpanded);
				Assert::AreEqual(base.left,final.presentation.sourceBounds.left);
				Assert::AreEqual(base.right,final.presentation.sourceBounds.right);
				Assert::AreEqual(base.top,final.presentation.sourceBounds.top);
				Assert::AreEqual(base.bottom,final.presentation.sourceBounds.bottom);
			}
		}

		TEST_METHOD(SubtitleOccupancyDoesNotBecomeFullHeightPictureOrHorizontalFit)
		{
			const ActivePictureBounds base{176,356,3664,1804,3840,2160,3488.0/1448,ActivePictureBounds::BarAxes::BOTH};
			ActivePictureEvidence evidence;
			evidence.available=true; evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
			evidence.proposedBounds={188,364,3652,1828,3840,2160,2.36,ActivePictureBounds::BarAxes::NONE};
			evidence.trustedBounds={188,0,3652,2160,3840,2160,1.60,ActivePictureBounds::BarAxes::LEFT_RIGHT};
			ActivePicturePresentationRetentionEvidence retention;
			retention.analysisValid=retention.presentationValid=retention.expansionStripsAvailable=true;
			retention.expansionBase=base; retention.activePicture=evidence;
			retention.excludedHorizontalBandsPixelSafe=true;
			retention.outwardVisibleBoundsAvailable=true;
			retention.outwardVisibleBounds=base; retention.outwardVisibleBounds.bottom=1860;
			const auto physical=ResolvePresentationObservation(base,evidence,retention);
			Assert::AreEqual(base.top,physical.bounds.top);
			Assert::AreEqual(1860,physical.bounds.bottom);
			Assert::AreEqual(0,evidence.trustedBounds.top); // Logical evidence is untouched.
			VerticalBarContentInput text;
			text.lowerContent=true; text.lowerOccupiedDepth=38; text.lowerPeakSamples=444;
			text.lowerBarPixels=356; text.sampledColumns=3840; text.lowerRequiredShift=74;
			const auto subtitle=EvaluateVerticalBarContent(text);
			Assert::IsTrue(subtitle.lowerOverlayLike && subtitle.action==VerticalBarPresentationAction::TRANSLATE);
			Input crop=TrustedScopeCrop(); crop.geometry=base;
			crop.latestObservationSupportsCrop=false; crop.latestObservationClassification=evidence.classification;
			crop.barCropRefinementPending=true;
			crop.barCropRefinementHorizontalConflict=HasHorizontalCropRefinementConflict(false,false,true,true,false,physical.bounds,base);
			crop.verticalTranslationActive=true; crop.verticalTranslationBase=base;
			crop.verticalTranslationPixels=subtitle.translationPixels; crop.verticalTranslationSourceGeneration=7;
			const auto d=Evaluate(crop);
			Assert::IsTrue(d.applyCrop && d.verticallyTranslated);
			Assert::IsFalse(d.outwardExpanded);
			Assert::AreEqual(base.right-base.left,d.sourceBounds.right-d.sourceBounds.left);
			Assert::AreEqual(base.bottom-base.top,d.sourceBounds.bottom-d.sourceBounds.top);
			Assert::IsTrue(d.sourceBounds.bottom>=1860);
			for (int failure=0; failure<6; ++failure)
			{
				auto bad=retention;
				switch(failure) {
				case 0: bad.analysisValid=false; break;
				case 1: bad.presentationValid=false; break;
				case 2: bad.expansionBase.top+=4; break;
				case 3: bad.activePicture.proposedBounds.bottom+=4; break;
				case 4: bad.outwardVisibleBoundsAvailable=false; break;
				case 5: bad.outwardVisibleBounds.right=base.right-4; break;
				}
				const auto rejected=ResolvePresentationObservation(base,evidence,bad);
				Assert::AreEqual(0,rejected.bounds.top);
				Assert::AreEqual(2160,rejected.bounds.bottom);
			}
		}

		TEST_METHOD(MovingAllSidedInsetCannotEarnAuthorityAcrossPartialAxisConfidence)
		{
			// Sequence timing from the 15:56:00-15:56:04 replay. Interpolate the
			// unlogged positions; this is an admission/model regression, not pixels.
			const ActivePictureBounds base{0,232,3840,1928,3840,2160,
				3840.0/1696,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			for (int failure = 0; failure < 10; ++failure)
			{
				ActivePictureTransitionModel model;
				for (uint64_t seq=761; seq<=764; ++seq)
					model.Observe({base,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
				uint64_t published = 0;
				for (uint64_t seq=765; seq<=950; ++seq)
				{
					TransitionAdmissionInput input;
					input.trustedGeometry = input.presentationBeforeObservation = base;
					input.trustedGeometryAvailable = input.compatiblePresentation = input.evidence.available = true;
					input.sourceGeneration = input.trustedGeneration = 7;
					input.sourceSequence = seq; input.framesPerSecond = 24;
					input.retention.analysisValid = input.retention.presentationValid = true;
					input.evidence.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
					const int x = seq < 849 ? 40 + 2*int((seq-765)*60/83) : 172;
					const int y = seq < 849 ? 244 + 2*int((seq-765)*50/83) : 352;
					auto candidate = base;
					candidate.left=x; candidate.right=3840-x;
					candidate.top=y; candidate.bottom=2160-y;
					candidate.aspectRatio=double(candidate.right-candidate.left)/(candidate.bottom-candidate.top);
					candidate.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
					input.evidence.trustedBounds = input.evidence.proposedBounds = candidate;
					const bool partial = seq>=849 && seq<=865;
					if (partial)
					{
						input.evidence.trustedBounds.top=0; input.evidence.trustedBounds.bottom=2160;
						input.evidence.trustedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::LEFT_RIGHT;
						input.evidence.proposedBounds.bottom=1828;
						ActivePictureEdgeEvidence edge;
						edge.barPixels=172; edge.lumaFloor=edge.lumaP90=64;
						edge.blackFraction=edge.continuity=edge.neutralChromaFraction=1;
						input.evidence.left=input.evidence.right=input.evidence.top=input.evidence.bottom=edge;
						if (seq==849)
						{
							switch (failure)
							{
							case 1: input.retention.globalNearBlack=true; break;
							case 2: input.retention.analysisValid=false; break;
							case 3: input.trustedGeneration=6; break;
							case 4: input.evidence.bottom.blackFraction=0.4; break;
							case 5: input.evidence.left.texture=20; break;
							case 6: input.evidence.proposedBounds.left=600; break;
							case 7: model.ResetCandidateEvidence(); break;
							case 8: input.retention.presentationValid=false; break;
							case 9: input.evidence.top.neutralChromaFraction=0.5; break;
							}
						}
					}
					input.outwardCandidate=input.evidence.trustedBounds;
					const auto admission=EvaluateTransitionAdmission(input);
					const auto decision=model.Observe(admission.observation);
					if (partial) Assert::IsFalse(decision.publish);
					if (decision.publish) { published=seq; break; }
				}
				// Neither uninterrupted time nor partial-axis evidence makes an
				// all-sided inner composition authoritative over the movie frame.
				Assert::AreEqual(uint64_t(0),published);
			}
		}

		TEST_METHOD(DeferredEvidenceCannotPromoteAllSidedInset)
		{
			for (int interruption=0; interruption<3; ++interruption)
			{
				ActivePictureTransitionModel model;
				auto base=TrustedScopeCrop().geometry;
				for (uint64_t seq=1;seq<=4;++seq)
					model.Observe({base,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
				auto nested=base; nested.left=192; nested.right=3648;
				nested.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
				nested.aspectRatio=double(nested.right-nested.left)/(nested.bottom-nested.top);
				for (uint64_t seq=5;seq<=76;++seq)
					Assert::IsFalse(model.Observe({nested,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24}).publish);
				uint64_t seq=77;
				if (interruption==1) seq+=10;
				for (int n=0;n<(interruption==2 ? 120 : 12);++n,++seq)
				{
					ActivePictureObservation partial{nested,seq,true,ActivePictureClassification::PROVISIONAL,24};
					partial.transitionDeferred=true;
					const auto d=model.Observe(partial);
					Assert::IsFalse(d.publish || d.knownTrustedGeometryReacquired);
				}
				const uint64_t resumed=seq;
				uint64_t published=0;
				for (;seq<resumed+100;++seq)
				{
					const auto d=model.Observe({nested,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
					if (d.publish) { published=seq; break; }
				}
				Assert::AreEqual(uint64_t(0),published);
			}
		}

		TEST_METHOD(SamplingJitterDoesNotRequireNewAspectAuthority)
		{
			const ActivePictureBounds base{192,372,3648,1788,3840,2160,2.44,ActivePictureBounds::BarAxes::BOTH};
			TransitionAdmissionInput input;
			input.trustedGeometry = input.presentationBeforeObservation = base;
			input.trustedGeometryAvailable = input.compatiblePresentation = input.evidence.available = true;
			input.sourceGeneration = input.trustedGeneration = 7;
			input.evidence.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.evidence.trustedBounds = base; input.evidence.trustedBounds.right += 4;
			input.outwardCandidate = input.evidence.trustedBounds;
			input.retention.analysisValid = input.retention.presentationValid = true;
			ActivePictureTransitionModel model;
			for (uint64_t seq=1; seq<=4; ++seq)
				model.Observe({base,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
			for (uint64_t seq=5; seq<65; ++seq)
			{
				input.sourceSequence = seq;
				const auto d = EvaluateTransitionAdmission(input);
				Assert::IsFalse(d.observation.transitionDeferred);
				Assert::IsFalse(model.Observe(d.observation).publish);
			}
			// Full-height subtitle contamination is still a material change.
			input.outwardCandidate.top = 0; input.outwardCandidate.bottom = 2160;
			Assert::IsTrue(EvaluateTransitionAdmission(input).observation.transitionDeferred);
		}


        TEST_METHOD(InspectionReentryCannotContractAnOccupiedOutwardFit)
        {
            // Captured replay 4524 -> 4525: the dense FIT expires while the
            // pixels still occupy both former bars. Inspection is permission
            // to hold a picture, not permission to restore an older zoom.
            auto input = ExpiredFitInspection();
            auto& crop = input.crop;
            auto admission = input.previousAdmission;
            for (uint64_t seq = 4525; seq <= 4528; ++seq)
            {
                crop.frameSourceSequence = crop.currentVisibleSourceSequence =
                    crop.verticalInspectionSourceSequence = input.retentionSourceSequence = seq;
                crop.presentationFailOpen = seq == 4528;
                input.candidate = Evaluate(crop);
                const auto recovered = EvaluatePresentationRecovery(input);
                const auto shown = AdmitCropPresentation(admission, crop, recovered.presentation, 9);
                Assert::IsFalse(shown.blocked);
                Assert::AreEqual(34, shown.presentation.sourceBounds.top,
                    L"Inspection re-entry must not flash back to logical scope or trim the existing fit.");
                Assert::AreEqual(2126, shown.presentation.sourceBounds.bottom);
                Assert::IsTrue(recovered.state.active);
                Assert::AreEqual(276, recovered.state.trustedCrop.top);
                input.previous = recovered.state;
                admission = shown.state;
                input.previousAdmission = admission;
            }
        }


        TEST_METHOD(InspectionReentryExposesLargerContentAndRejectsMissingExtentProof)
        {
            for (int variant = 0; variant < 4; ++variant)
            {
                auto input = ExpiredFitInspection();
                if (variant == 0) { input.crop.currentVisibleBounds.top = 12; input.crop.currentVisibleBounds.bottom = 2148; }
                if (variant == 1) { input.crop.currentVisibleBounds.top = 0; input.crop.currentVisibleBounds.bottom = 2160; }
                if (variant == 2) input.crop.currentVisibleBoundsAvailable = false;
                if (variant == 3) --input.crop.currentVisibleSourceSequence;
                const auto result = EvaluatePresentationRecovery(input);
                Assert::IsTrue(result.started);
                Assert::AreEqual(variant == 0 ? 12 : 0, result.presentation.sourceBounds.top);
                Assert::AreEqual(variant == 0 ? 2148 : 2160, result.presentation.sourceBounds.bottom);
            }
        }

        TEST_METHOD(InspectionReentryRepeatPreservesFitWithoutCountingProof)
        {
            auto input = ExpiredFitInspection();
            input.cadenceRepeat = true;
            input.previousAdmission.presentationSourceSequence = input.crop.frameSourceSequence;
            auto result = EvaluatePresentationRecovery(input);
            Assert::IsTrue(result.started);
            Assert::AreEqual(34, result.presentation.sourceBounds.top);
            Assert::AreEqual(0u, result.samples);
            Assert::IsTrue((result.gates & RECOVERY_REPEAT) != 0);
            // Once current bars are safe, repeats still cannot release inward.
            input.previous = result.state;
            input.excludedBandsPixelSafe = true;
            input.observation = input.observedTrustedCrop = input.crop.geometry;
            input.crop.latestObservationClassification = input.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            input.crop.latestObservationSupportsCrop = true;
            input.crop.latestObservationIsProvisional = false;
            input.candidate = Evaluate(input.crop);
            result = EvaluatePresentationRecovery(input);
            Assert::IsFalse(result.released);
            Assert::AreEqual(0u, result.samples);
            Assert::AreEqual(34, result.presentation.sourceBounds.top);
        }

        TEST_METHOD(InspectionReentryRequiresCurrentContinuousMatchingFit)
        {
            for (int fault = 0; fault < 22; ++fault)
            {
                auto input = ExpiredFitInspection();
                switch (fault)
                {
                case 0: input.previousAdmission.available = false; break;
                case 1: input.previousAdmission.outwardPresentationAvailable = false; break;
                case 2: --input.previousAdmission.sourceGeneration; break;
                case 3: ++input.previousAdmission.presentationEpoch; break;
                case 4: input.previousAdmission.trustedCrop.top += 2; break;
                case 5: input.previousAdmission.trustedCrop.rasterWidth /= 2; break;
                case 6: input.previousAdmission.presentationSourceSequence = 0; break;
                case 7: --input.previousAdmission.presentationSourceSequence; break;
                case 8: input.previousAdmission.presentationSourceSequence += 2; break;
                case 9: input.previousAdmission.outwardPresentation.top = -2; break;
                case 10: ++input.previousAdmission.outwardPresentation.top; break;
                case 11: input.previousAdmission.outwardPresentation.top = 278; break;
                case 12: input.previousAdmission.outwardPresentation = input.crop.geometry; break;
                case 13: input.measurementCurrent = false; break;
                case 14: input.retentionEvaluated = false; break;
                case 15: --input.retentionSourceGeneration; break;
                case 16: --input.retentionSourceSequence; break;
                case 17: input.retentionBounds.top += 2; break;
                case 18: input.excludedBandsPixelSafe = true; break;
                case 19: input.nearBlackEvaluated = false; break;
                case 20: input.globalNearBlack = true; break;
                case 21: input.crop.latestObservationSupportsCrop = true; break;
                }
                const auto result = EvaluatePresentationRecovery(input);
                const auto message = L"Unexpected inspection recovery for fault " + std::to_wstring(fault);
                Assert::IsFalse(result.started, message.c_str());
            }
        }

        TEST_METHOD(InspectionReentryPreservesConfirmedOwnerPrecedence)
        {
            for (int variant = 0; variant < 5; ++variant)
            {
                auto input = ExpiredFitInspection();
                if (variant == 0) input.crop.automaticCropEnabled = false;
                if (variant == 1) input.crop.fullRasterPresentationAuthoritative = true;
                if (variant == 2) input.crop.movingPictureTransition = true;
                if (variant == 3) input.crop.nearBlackEpisodeFullRaster = true;
                if (variant == 4) input.confirmedPresentationResolved = true;
                input.candidate = Evaluate(input.crop);
                const auto result = EvaluatePresentationRecovery(input);
                Assert::IsFalse(result.started);
                Assert::AreEqual(input.candidate.sourceBounds.top, result.presentation.sourceBounds.top);
            }
        }


        TEST_METHOD(InspectionReentryDoesNotDelayNativeResolutionInsideBridge)
        {
            for (int fault = -1; fault < 7; ++fault)
            {
                auto input = ExpiredFitInspection();
                input.previous = EvaluatePresentationRecovery(input).state;
                ++input.crop.frameSourceSequence;
                input.retentionSourceSequence = input.crop.frameSourceSequence;
                input.crop.currentVisibleBoundsAvailable = false;
                input.crop.verticalInspectionPending = false;
                input.excludedBandsPixelSafe = true;
                input.crop.latestObservationSupportsCrop = true;
                input.crop.latestObservationIsProvisional = false;
                input.crop.latestObservationClassification = input.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                input.observation = input.observedTrustedCrop = input.crop.geometry;
                switch (fault)
                {
                case 0: input.excludedBandsPixelSafe = false; break;
                case 1: --input.retentionSourceSequence; break;
                case 2: input.globalNearBlack = true; break;
                case 3: input.cadenceRepeat = true; break;
                case 4: input.observationAvailable = false; break;
                case 5: input.crop.latestObservationSupportsCrop = false; break;
                case 6: input.observedTrustedCrop.top += 40; break;
                }
                input.candidate = Evaluate(input.crop);
                const auto result = EvaluatePresentationRecovery(input);
                Assert::AreEqual(fault == -1, result.released);
                if (fault == -1) Assert::AreEqual(276, result.presentation.sourceBounds.top);
                else Assert::IsTrue(result.presentation.sourceBounds.top <= 34 &&
                    result.presentation.sourceBounds.bottom >= 2126,
                    L"Unproven native resolution may expose raster, but cannot contract the held fit.");
            }
        }



        TEST_METHOD(InspectionReentrySafeResolutionSurvivesRepeatWithoutAddingDwell)
        {
            for (bool provisional : {false, true})
            for (bool markedRepeat : {false, true})
            {
                auto input = ExpiredFitInspection();
                auto result = EvaluatePresentationRecovery(input);
                // A second inspected source frame is still inside the bridge.
                input.previous = result.state;
                ++input.crop.frameSourceSequence;
                input.retentionSourceSequence = input.crop.currentVisibleSourceSequence =
                    input.crop.verticalInspectionSourceSequence = input.crop.frameSourceSequence;
                input.candidate = Evaluate(input.crop);
                result = EvaluatePresentationRecovery(input);
                input.previous = result.state;
                input.cadenceRepeat = markedRepeat;
                input.crop.verticalInspectionPending = false;
                input.crop.currentVisibleBoundsAvailable = false;
                input.excludedBandsPixelSafe = true;
                input.crop.frameLocalPresentationRetentionSafe = true;
                input.crop.latestObservationSupportsCrop = !provisional;
                input.crop.latestObservationIsProvisional = provisional;
                input.crop.latestObservationClassification = input.observationClassification = provisional
                    ? ActivePictureClassification::PROVISIONAL : ActivePictureClassification::BAR_CROP_TRUSTED;
                input.observation = input.observedTrustedCrop = input.crop.geometry;
                input.candidate = Evaluate(input.crop);
                result = EvaluatePresentationRecovery(input);
                Assert::IsFalse(result.released, L"Even an unflagged same-sequence call cannot resolve the bridge.");
                Assert::AreEqual(0u, result.samples);
                Assert::IsTrue(result.state.inspectionReentryPending);
                Assert::AreEqual(34, result.presentation.sourceBounds.top);
                input.previous = result.state;
                input.cadenceRepeat = false;
                ++input.crop.frameSourceSequence;
                input.retentionSourceSequence = input.crop.frameSourceSequence;
                input.candidate = Evaluate(input.crop);
                result = EvaluatePresentationRecovery(input);
                Assert::IsTrue(result.released, L"The next current safe native or provisional frame keeps the original immediate bridge exit.");
                Assert::AreEqual(276, result.presentation.sourceBounds.top);
            }
        }

        TEST_METHOD(InspectionNativeResolutionCannotCrossContractOrSurviveAnotherOwner)
        {
            for (int variant = 0; variant < 3; ++variant)
            {
                auto input = ExpiredFitInspection();
                auto result = EvaluatePresentationRecovery(input);
                if (variant == 0)
                {
                    input.previous = result.state;
                    ++input.crop.frameSourceSequence;
                    input.retentionSourceSequence = input.crop.currentVisibleSourceSequence = input.crop.frameSourceSequence;
                    input.crop.verticalInspectionPending = false;
                    input.crop.presentationFailOpen = true;
                    input.candidate = Evaluate(input.crop);
                    result = EvaluatePresentationRecovery(input);
                    Assert::IsFalse(result.state.inspectionReentryPending);
                }
                if (variant == 2) result.state.inspectionReentryPending = false; // Ordinary pre-existing recovery.
                input.previous = result.state;
                ++input.crop.frameSourceSequence;
                input.retentionSourceSequence = input.crop.frameSourceSequence;
                input.crop.verticalInspectionPending = input.crop.presentationFailOpen = false;
                if (variant == 1)
                {
                    input.crop.geometry.top = 68; input.crop.geometry.bottom = 2092;
                    input.crop.geometry.aspectRatio = 3840.0 / 2024;
                }
                input.retentionBounds = input.observation = input.observedTrustedCrop = input.crop.geometry;
                input.crop.currentVisibleBoundsAvailable = false;
                input.excludedBandsPixelSafe = true;
                input.crop.latestObservationSupportsCrop = true;
                input.crop.latestObservationIsProvisional = false;
                input.crop.latestObservationClassification = input.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                input.candidate = Evaluate(input.crop);
                result = EvaluatePresentationRecovery(input);
                Assert::IsFalse(result.released);
                Assert::IsFalse(result.state.inspectionReentryPending);
                Assert::AreEqual(34, result.presentation.sourceBounds.top);
            }
        }

        TEST_METHOD(InspectionReentryReturnsAfterExistingSafeCropProof)
        {
            auto input = ExpiredFitInspection();
            auto result = EvaluatePresentationRecovery(input);
            Assert::IsTrue(result.started);
            input.previous = result.state;
            ++input.crop.frameSourceSequence;
            input.retentionSourceSequence = input.crop.currentVisibleSourceSequence = input.crop.frameSourceSequence;
            input.crop.verticalInspectionPending = false;
            input.crop.presentationFailOpen = true;
            input.candidate = Evaluate(input.crop);
            result = EvaluatePresentationRecovery(input);
            Assert::IsFalse(result.state.inspectionReentryPending);
            input.crop.presentationFailOpen = false;
            for (unsigned frame = 1; frame <= result.required; ++frame)
            {
                input.previous = result.state;
                ++input.crop.frameSourceSequence;
                input.retentionSourceSequence = input.crop.frameSourceSequence;
                input.crop.currentVisibleBoundsAvailable = false;
                input.excludedBandsPixelSafe = true;
                input.crop.latestObservationSupportsCrop = true;
                input.crop.latestObservationIsProvisional = false;
                input.crop.verticalInspectionPending = false;
                input.crop.latestObservationClassification = input.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                input.observation = input.observedTrustedCrop = input.crop.geometry;
                input.candidate = Evaluate(input.crop);
                result = EvaluatePresentationRecovery(input);
                Assert::AreEqual(frame == result.required, result.released);
                Assert::AreEqual(frame == result.required ? 276 : 34, result.presentation.sourceBounds.top);
            }
        }

        TEST_METHOD(AdmissionClearsPriorFitOnWithdrawalBlockedAndOtherOwners)
        {
            for (int variant = 0; variant < 5; ++variant)
            {
                auto input = ExpiredFitInspection();
                auto candidate = input.candidate;
                auto crop = input.crop;
                if (variant == 0) { crop.presentationFailOpen = true; candidate = Evaluate(crop); }
                if (variant == 1) { crop.geometry.top += 2; candidate = Evaluate(crop); }
                if (variant == 2) { crop.latestObservationSupportsCrop = true; crop.verticalInspectionPending = false; candidate = Evaluate(crop); }
                if (variant == 3) { crop.verticalInspectionPending = false; crop.verticalTranslationActive = true;
                    crop.verticalTranslationPixels = 100; crop.verticalTranslationBase = crop.geometry;
                    crop.verticalTranslationSourceGeneration = 7; candidate = Evaluate(crop); }
                if (variant == 4) { crop.fullRasterPresentationAuthoritative = true; candidate = Evaluate(crop); }
                const auto admitted = AdmitCropPresentation(input.previousAdmission, crop, candidate, 9);
                Assert::IsFalse(admitted.state.outwardPresentationAvailable);
                Assert::AreEqual(uint64_t{0}, admitted.state.presentationSourceSequence);
            }
        }

        TEST_METHOD(NearBlackWeakFringePreservesOnlyActuallyAdmittedOptionalFill)
        {
            for (int top : {38, 42})
            for (int mode = 0; mode < 3; ++mode)
            {
                const ActivePictureBounds base = {0,top,3840,top == 38 ? 2120 : 2118,
                    3840,2160,3840.0/(top == 38 ? 2082 : 2076),ActivePictureBounds::BarAxes::TOP_BOTTOM};
                auto crop = TrustedScopeCrop();
                crop.geometry = base;
                crop.frameSourceSequence = 100;
                crop.framePresentationEpoch = 9;
                auto admission = AdmitCropPresentation({},crop,Evaluate(crop),9).state;
                AspectLimitFillInput fill;
                fill.trustedContentAuthorityAccepted = fill.contentReferenceAvailable = true;
                fill.sourceBounds = fill.contentReferenceBounds = base;
                fill.screenAspect = mode == 2 ? 2.35 : 16.0/9.0;
                fill.cropWiderContentToFillScreen = mode != 1;
                fill.widerLimitConfigured = true;
                fill.widerAspectLimit = 1.86;
                const auto originalFill = EvaluateAspectLimitFill(fill);
                Assert::AreEqual(mode == 0,originalFill.applied);
                auto recordFill = [&](CropPresentationAdmissionState& state,
                    const AspectLimitFillDecision& applied,uint64_t sequence) {
                    state.optionalFillApplied = applied.applied;
                    state.optionalFillSourceBounds = applied.sourceBounds;
                    state.optionalFillSourceSequence = applied.applied ? sequence : 0;
                    state.optionalFillScreenAspect = fill.screenAspect;
                };
                recordFill(admission,originalFill,100);
                NearBlackPresentationEpisodeInput ep;
                ep.measurementCurrent = ep.nearBlackEvaluated = ep.globalNearBlack = ep.trustedCropAvailable = true;
                ep.trustedCrop = base;
                ep.sourceGeneration = crop.frameSourceGeneration;
                ep.presentationEpoch = 9;
                ep.sourceSequence = 100;
                ep.framesPerSecond = 24;
                auto state = EvaluateNearBlackPresentationEpisode(ep).state;
                for (uint64_t sequence = 101; sequence <= 110; ++sequence)
                {
                    const bool fringe = sequence == 102;
                    ep.previous = state;
                    ep.sourceSequence = ep.retentionSourceSequence = ep.reacquiredSourceSequence = sequence;
                    ep.globalNearBlack = sequence == 101;
                    ep.boundedVisibleContentOutsideCrop = fringe;
                    ep.currentObservationAvailable = ep.retentionEvaluated = true;
                    ep.retentionSourceGeneration = ep.reacquiredSourceGeneration = ep.sourceGeneration;
                    ep.retentionBounds = ep.reacquiredTrustedGeometry = base;
                    ep.currentObservation = base;
                    ep.currentObservation.top -= 2;
                    ep.currentObservationClassification = ep.reacquiredTrustedClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                    ep.knownTrustedGeometryReacquired = ep.reacquisitionIsCurrentAssociation = true;
                    ep.reacquiredPresentationEpoch = ep.presentationEpoch;
                    ep.retentionSafe = ep.retentionExcludedBandsPixelSafe = !fringe;
                    fill.sourceBounds = fill.contentReferenceBounds = base;
                    fill.trustedContentAuthorityAccepted = true;
                    ep.weakFringeFillRetained = CanRetainWeakFringeWithFill(ep,admission,fill,fringe,false);
                    if (fringe) Assert::AreEqual(mode == 0,ep.weakFringeFillRetained,
                        L"Only previously applied and still eligible fill can protect this weak fringe.");
                    const auto episode = EvaluateNearBlackPresentationEpisode(ep);
                    ActivePictureEvidence raw;
                    raw.available = true;
                    raw.classification = ep.currentObservationClassification;
                    const auto classified = ConstrainNearBlackCropAcquisition(raw,
                        ep.previous.mode != NearBlackPresentationMode::INACTIVE);
                    crop.frameSourceSequence = sequence;
                    crop.latestObservationClassification = classified.classification;
                    crop.latestObservationIsProvisional = classified.classification == ActivePictureClassification::PROVISIONAL;
                    crop.latestObservationSupportsCrop = !crop.latestObservationIsProvisional;
                    crop.frameLocalPresentationRetentionEvaluated = true;
                    crop.frameLocalPresentationRetentionSafe = !fringe;
                    crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(episode);
                    crop.nearBlackEpisodeFullRaster = episode.state.mode == NearBlackPresentationMode::FULL_RASTER;
                    crop.currentVisibleBoundsAvailable = fringe;
                    crop.currentVisibleBase = base;
                    crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
                    crop.currentVisibleSourceSequence = sequence;
                    crop.currentVisibleBounds = base;
                    crop.currentVisibleBounds.top -= 15;
                    PresentationRecoveryInput recovery;
                    recovery.crop = crop;
                    recovery.candidate = Evaluate(crop);
                    recovery.previousAdmission = admission;
                    recovery.presentationEpoch = ep.presentationEpoch;
                    recovery.measurementCurrent = recovery.retentionEvaluated = recovery.nearBlackEvaluated = true;
                    recovery.globalNearBlack = ep.globalNearBlack;
                    recovery.excludedBandsPixelSafe = !fringe;
                    recovery.retentionSourceGeneration = ep.sourceGeneration;
                    recovery.retentionSourceSequence = sequence;
                    recovery.retentionBounds = base;
                    recovery.observationAvailable = true;
                    recovery.observation = ep.currentObservation;
                    recovery.observationClassification = ep.currentObservationClassification;
                    recovery.confirmedPresentationResolved = episode.releasedToTrustedCrop;
                    const auto recovered = EvaluatePresentationRecovery(recovery);
                    recovery.previous = recovered.state;
                    recovery.candidate = recovered.presentation;
                    const auto bounded = EvaluateNearBlackBoundedPresentation(episode.state,recovery);
                    state = CommitNearBlackForTest(bounded,episode.state,admission,sequence);
                    const auto admitted = AdmitCropPresentation(admission,crop,bounded.presentation,9);
                    Assert::IsFalse(admitted.blocked);
                    admission = admitted.state;
                    Assert::IsTrue(admitted.presentation.applyCrop);
                    fill.sourceBounds = admitted.presentation.sourceBounds;
                    // Match the renderer: recovery's full-raster episode cannot
                    // regain optional fill merely because its fallback is bounded.
                    fill.trustedContentAuthorityAccepted = admitted.presentation.applyCrop && !crop.nearBlackEpisodeFullRaster;
                    const auto finalFill = EvaluateAspectLimitFill(fill);
                    recordFill(admission,finalFill,sequence);
                    const auto selected = finalFill.applied ? finalFill.sourceBounds : admitted.presentation.sourceBounds;
                    if (mode == 0)
                    {
                        Assert::IsTrue(finalFill.applied);
                        Assert::AreEqual(originalFill.sourceBounds.left,selected.left);
                        Assert::AreEqual(originalFill.sourceBounds.top,selected.top);
                        Assert::AreEqual(originalFill.sourceBounds.right,selected.right);
                        Assert::AreEqual(originalFill.sourceBounds.bottom,selected.bottom);
                    }
                    else
                    {
                        Assert::IsFalse(finalFill.applied);
                        Assert::AreEqual(sequence >= 102 && sequence <= 108 ? top-16 : top,selected.top);
                        Assert::AreEqual(base.bottom,selected.bottom);
                    }
                    const auto source = ResolveNlsSourceGeometry(true,selected.left,selected.top,
                        selected.right,selected.bottom,3840,2160);
                    const auto layout = FitAspect(source.aspect,{0,0,3840,3840/fill.screenAspect},VerticalPictureAlignment::BOTTOM);
                    Assert::IsTrue(layout.valid);
                    Assert::AreEqual(3840/fill.screenAspect,layout.picture.bottom,0.001);
                    Assert::AreEqual(sequence == 109,episode.releasedToTrustedCrop);
                    Assert::AreEqual(top,crop.geometry.top);
                }
            }
        }

        TEST_METHOD(NearBlackWeakFringeFillCertificateRejectsMissingChangedOrCompetingContracts)
        {
            auto original = ReaffirmedRetainedScope();
            original.trustedCrop = {0,42,3840,2118,3840,2160,3840.0/2076,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            original.previous.entryTrustedCrop = original.trustedCrop;
            original.sourceSequence = original.retentionSourceSequence = 1963;
            original.retentionBounds = original.currentObservation = original.trustedCrop;
            original.currentObservation.top = 40;
            original.boundedVisibleContentOutsideCrop = true;
            original.retentionSafe = original.retentionExcludedBandsPixelSafe = false;
            AspectLimitFillInput initialFill;
            initialFill.trustedContentAuthorityAccepted = initialFill.contentReferenceAvailable = true;
            initialFill.sourceBounds = initialFill.contentReferenceBounds = original.trustedCrop;
            initialFill.screenAspect = 16.0/9.0;
            initialFill.cropWiderContentToFillScreen = initialFill.widerLimitConfigured = true;
            initialFill.widerAspectLimit = 1.86;
            const auto rendered = EvaluateAspectLimitFill(initialFill);
            Assert::IsTrue(rendered.applied);
            CropPresentationAdmissionState admitted;
            admitted.available = admitted.optionalFillApplied = true;
            admitted.trustedCrop = original.trustedCrop;
            admitted.sourceGeneration = original.sourceGeneration;
            admitted.presentationEpoch = original.presentationEpoch;
            admitted.optionalFillSourceSequence = admitted.presentationSourceSequence = 1962;
            admitted.optionalFillScreenAspect = initialFill.screenAspect;
            admitted.optionalFillSourceBounds = rendered.sourceBounds;
            Assert::IsTrue(CanRetainWeakFringeWithFill(original,admitted,initialFill,true,false));
            for (int invalid = 0; invalid < 23; ++invalid)
            {
                auto ep = original;
                auto prior = admitted;
                auto fill = initialFill;
                bool weak = true, competing = false;
                switch (invalid)
                {
                case 0: fill.cropWiderContentToFillScreen = false; break;
                case 1: fill.widerAspectLimit = 1.84; break;
                case 2: prior.optionalFillApplied = false; break;
                case 3: prior.available = false; break;
                case 4: fill.screenAspect = 2.35; break;
                case 5: ++prior.presentationEpoch; break;
                case 6: prior.trustedCrop.top += 2; break;
                case 7: ++prior.sourceGeneration; break;
                case 8: --prior.optionalFillSourceSequence; break;
                case 9: prior.optionalFillSourceSequence = ep.sourceSequence; break;
                case 10: ep.fullRasterAuthorityAvailable = true; break;
                case 11: competing = true; break;
                case 12: weak = false; break;
                case 13: ep.cadenceRepeat = true; break;
                case 14: ep.previous.mode = NearBlackPresentationMode::FULL_RASTER; break;
                case 15: ep.previous.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT; break;
                case 16: ep.measurementCurrent = false; break;
                case 17: --ep.retentionSourceSequence; break;
                case 18: ep.retentionBounds.top += 2; break;
                case 19: fill.trustedContentAuthorityAccepted = false; break;
                case 20: prior.optionalFillSourceBounds.left += 2; break;
                case 21: ep.sceneBoundary = true; break;
                case 22: ep.currentObservationClassification = ActivePictureClassification::FULL_RASTER_TRUSTED; break;
                }
                Assert::IsFalse(CanRetainWeakFringeWithFill(ep,prior,fill,weak,competing));
            }
        }
        TEST_METHOD(NearBlackWeakFringeFillReusesOnlyItsCertifiedRepeatedFrame)
        {
            auto ep = ReaffirmedRetainedScope();
            ep.trustedCrop = {0,42,3840,2118,3840,2160,3840.0/2076,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            ep.previous.entryTrustedCrop = ep.trustedCrop;
            ep.sourceSequence = ep.retentionSourceSequence = 1963;
            ep.retentionBounds = ep.currentObservation = ep.trustedCrop;
            ep.currentObservation.top = 40;
            ep.boundedVisibleContentOutsideCrop = true;
            ep.retentionSafe = ep.retentionExcludedBandsPixelSafe = false;
            AspectLimitFillInput fill;
            fill.trustedContentAuthorityAccepted = fill.contentReferenceAvailable = true;
            fill.sourceBounds = fill.contentReferenceBounds = ep.trustedCrop;
            fill.screenAspect = 16.0/9.0;
            fill.cropWiderContentToFillScreen = fill.widerLimitConfigured = true;
            fill.widerAspectLimit = 1.86;
            const auto rendered = EvaluateAspectLimitFill(fill);
            CropPresentationAdmissionState prior;
            prior.available = prior.optionalFillApplied = true;
            prior.trustedCrop = ep.trustedCrop;
            prior.sourceGeneration = ep.sourceGeneration;
            prior.presentationEpoch = ep.presentationEpoch;
            prior.optionalFillSourceSequence = 1962;
            prior.optionalFillScreenAspect = fill.screenAspect;
            prior.optionalFillSourceBounds = rendered.sourceBounds;
            ep.weakFringeFillRetained = CanRetainWeakFringeWithFill(ep,prior,fill,true,false);
            Assert::IsTrue(ep.weakFringeFillRetained);
            const auto certified = EvaluateNearBlackPresentationEpisode(ep);
            Assert::AreEqual(ep.sourceSequence,certified.state.weakFringeFillSourceSequence);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(certified.state.mode));
            ep.previous = certified.state;
            ep.cadenceRepeat = true;
            prior.optionalFillSourceSequence = ep.sourceSequence;
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                ep.weakFringeFillRetained = CanRetainWeakFringeWithFill(ep,prior,fill,true,false);
                Assert::IsTrue(ep.weakFringeFillRetained);
                const auto replayed = EvaluateNearBlackPresentationEpisode(ep);
                Assert::IsFalse(replayed.changedToFullRaster);
                Assert::AreEqual(certified.state.revalidationSamples,replayed.state.revalidationSamples);
                Assert::AreEqual(certified.state.weakFringeFillSourceSequence,replayed.state.weakFringeFillSourceSequence);
                ep.previous = replayed.state;
            }
            auto uncertified = ep;
            uncertified.previous.weakFringeFillSourceSequence = 0;
            Assert::IsFalse(CanRetainWeakFringeWithFill(uncertified,prior,fill,true,false));
            auto differentFrame = ep;
            ++differentFrame.sourceSequence;
            ++differentFrame.retentionSourceSequence;
            prior.optionalFillSourceSequence = differentFrame.sourceSequence;
            Assert::IsFalse(CanRetainWeakFringeWithFill(differentFrame,prior,fill,true,false));
        }

        TEST_METHOD(NearBlackSameObservedRowsRemainStableWithDifferentAcquisitionHistory)
        {
            // Both histories receive the SAME observed 40..2118 picture. Rows
            // 40/41 are inside the earlier 38 crop and outside the later 42 crop.
            const ActivePictureBounds observed = {0,40,3840,2118,3840,2160,3840.0/2078,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            for (int top : {38,42})
            {
                auto ep = ReaffirmedRetainedScope();
                ep.trustedCrop = {0,top,3840,top == 38 ? 2120 : 2118,3840,2160,
                    3840.0/(top == 38 ? 2082 : 2076),ActivePictureBounds::BarAxes::TOP_BOTTOM};
                ep.previous.entryTrustedCrop = ep.trustedCrop;
                ep.sourceSequence = ep.retentionSourceSequence = 1963;
                ep.retentionBounds = ep.trustedCrop;
                ep.currentObservation = observed;
                ep.boundedVisibleContentOutsideCrop = top == 42;
                ep.retentionSafe = ep.retentionExcludedBandsPixelSafe = top == 38;
                AspectLimitFillInput fill;
                fill.trustedContentAuthorityAccepted = fill.contentReferenceAvailable = true;
                fill.sourceBounds = fill.contentReferenceBounds = ep.trustedCrop;
                fill.screenAspect = 16.0/9.0;
                fill.cropWiderContentToFillScreen = fill.widerLimitConfigured = true;
                fill.widerAspectLimit = 1.86;
                const auto original = EvaluateAspectLimitFill(fill);
                Assert::IsTrue(original.applied);
                CropPresentationAdmissionState prior;
                prior.available = prior.optionalFillApplied = true;
                prior.trustedCrop = ep.trustedCrop;
                prior.sourceGeneration = ep.sourceGeneration;
                prior.presentationEpoch = ep.presentationEpoch;
                prior.optionalFillSourceSequence = 1962;
                prior.optionalFillScreenAspect = fill.screenAspect;
                prior.optionalFillSourceBounds = original.sourceBounds;
                ep.weakFringeFillRetained = CanRetainWeakFringeWithFill(ep,prior,fill,top == 42,false);
                Assert::AreEqual(top == 42,ep.weakFringeFillRetained);
                const auto episode = EvaluateNearBlackPresentationEpisode(ep);
                Assert::IsFalse(episode.changedToFullRaster);
                auto crop = TrustedScopeCrop();
                crop.geometry = ep.trustedCrop;
                crop.geometrySourceGeneration = crop.frameSourceGeneration = ep.sourceGeneration;
                crop.frameSourceSequence = ep.sourceSequence;
                crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(episode);
                crop.latestObservationSupportsCrop = false;
                crop.latestObservationIsProvisional = true;
                crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
                crop.frameLocalPresentationRetentionEvaluated = true;
                crop.frameLocalPresentationRetentionSafe = ep.retentionSafe;
                const auto admitted = AdmitCropPresentation(prior,crop,Evaluate(crop),ep.presentationEpoch);
                Assert::IsFalse(admitted.blocked);
                Assert::IsTrue(admitted.presentation.applyCrop);
                fill.sourceBounds = admitted.presentation.sourceBounds;
                const auto result = EvaluateAspectLimitFill(fill);
                Assert::IsTrue(result.applied);
                Assert::AreEqual(original.sourceBounds.left,result.sourceBounds.left);
                Assert::AreEqual(original.sourceBounds.top,result.sourceBounds.top);
                Assert::AreEqual(original.sourceBounds.right,result.sourceBounds.right);
                Assert::AreEqual(original.sourceBounds.bottom,result.sourceBounds.bottom);
            }
        }
        TEST_METHOD(NearBlackBoundedReplayKeepsMeasuredFringeWithoutFullRasterFlash)
        {
            auto input = ToyStoryNearBlackRecovery();
            const auto episode = ToyStoryRetainedEpisode(input);
            Assert::IsTrue(episode.mode == NearBlackPresentationMode::FULL_RASTER);
            AssertFullRaster(input.candidate); // Actual pre-fix episode presentation.
            const auto result = EvaluateNearBlackBoundedPresentation(episode, input);
            Assert::IsTrue(result.boundedPresentation);
            Assert::IsTrue(result.presentation.applyCrop);
            Assert::AreEqual(26, result.presentation.sourceBounds.top);
            Assert::AreEqual(2118, result.presentation.sourceBounds.bottom);
            Assert::IsTrue(result.state.mode == NearBlackPresentationMode::FULL_RASTER);
            Assert::AreEqual(42, result.state.entryTrustedCrop.top); // No new logical AR.
            Assert::IsFalse(AdmitCropPresentation(input.previousAdmission, input.crop,
                result.presentation, input.presentationEpoch).blocked);
        }
        TEST_METHOD(NearBlackBoundedReplayKeepsSafeEnvelopeAcrossTwoPixelObservationDisagreement)
        {
            auto input = ToyStoryNearBlackRecovery();
            // Native analysis stays BAR_CROP_TRUSTED; episode acquisition suppresses
            // only its publication. Reproduce both classifications from the renderer.
            ActivePictureEvidence raw;
            raw.available = true;
            raw.classification = input.observationClassification;
            const auto constrained = ConstrainNearBlackCropAcquisition(raw, true);
            input.crop.latestObservationClassification = constrained.classification;
            input.crop.latestObservationIsProvisional = true;
            const auto first = EvaluateNearBlackBoundedPresentation(ToyStoryRetainedEpisode(input), input);
            Assert::IsTrue(first.boundedPresentation);
            Assert::AreEqual(26, first.presentation.sourceBounds.top);
            auto state = CommitNearBlackForTest(first, first.state,
                input.previousAdmission, input.crop.frameSourceSequence);
            Assert::IsTrue(state.boundedPresentationAvailable);
            for (int frame = 1; frame <= 6; ++frame)
            {
                ++input.crop.frameSourceSequence;
                input.retentionSourceSequence = input.crop.frameSourceSequence;
                input.crop.currentVisibleBoundsAvailable = false;
                input.excludedBandsPixelSafe = true;
                input.observation.top = 40; // Safe inside displayed 26, outside logical 42.
                const auto next = EvaluateNearBlackBoundedPresentation(state, input);
                Assert::IsTrue(next.boundedPresentation,
                    L"A safe observed edge inside the admitted envelope must not flash full raster.");
                Assert::AreEqual(26, next.presentation.sourceBounds.top);
                Assert::AreEqual(2118, next.presentation.sourceBounds.bottom);
                Assert::AreEqual(42, next.state.entryTrustedCrop.top);
                Assert::IsFalse(next.state.boundedPresentationFailed);
                state = CommitNearBlackForTest(next, state,
                    input.previousAdmission, input.crop.frameSourceSequence);
                Assert::AreEqual(input.crop.frameSourceSequence,
                    state.boundedPresentationPresentedSequence);
            }
        }
        TEST_METHOD(PreviouslyAdmittedScopeStaysLogicalAnchorWhenDarkFrameHasOutwardPixels)
        {
            const ActivePictureBounds scope{30,276,3808,1884,3840,2160,
                3778.0/1608.0,ActivePictureBounds::BarAxes::BOTH};
            const ActivePictureBounds visible{0,150,3840,1884,3840,2160,
                3840.0/1734.0,ActivePictureBounds::BarAxes::NONE};
            auto recovery=ToyStoryNearBlackRecovery(5118);
            recovery.crop.geometry=scope;
            recovery.crop.currentVisibleBase=scope;
            recovery.crop.currentVisibleBounds=visible;
            recovery.crop.currentVisibleBoundsAvailable=true;
            recovery.crop.currentVisibleSourceGeneration=recovery.crop.frameSourceGeneration;
            recovery.crop.currentVisibleSourceSequence=recovery.crop.frameSourceSequence;
            recovery.crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            recovery.crop.geometrySourceGeneration=recovery.crop.frameSourceGeneration;
            recovery.crop.nearBlackEpisodeFullRaster=false;
            recovery.crop.nearBlackEpisodeRetainCrop=true;
            recovery.crop.latestObservationIsProvisional=true;
            recovery.crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            recovery.crop.latestObservationSupportsCrop=false;
            recovery.observation=visible;
            recovery.observationClassification=ActivePictureClassification::PROVISIONAL;
            recovery.retentionBounds=scope;
            recovery.previousAdmission.trustedCrop=scope;

            NearBlackPresentationEpisodeInput episodeInput;
            episodeInput.measurementCurrent=episodeInput.nearBlackEvaluated=
                episodeInput.globalNearBlack=episodeInput.trustedCropAvailable=true;
            episodeInput.trustedCrop=scope;
            episodeInput.sourceGeneration=recovery.crop.frameSourceGeneration;
            episodeInput.sourceSequence=recovery.crop.frameSourceSequence;
            episodeInput.presentationEpoch=recovery.presentationEpoch;
            episodeInput.boundedVisibleContentOutsideCrop=true;
            episodeInput.previouslyAdmittedCropAvailable=true;
            episodeInput.previouslyAdmittedCrop=scope;
            episodeInput.previouslyAdmittedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;
            episodeInput.previouslyAdmittedCropSourceGeneration=episodeInput.sourceGeneration;
            episodeInput.previouslyAdmittedCropPresentationEpoch=episodeInput.presentationEpoch;
            const auto episode=EvaluateNearBlackPresentationEpisode(episodeInput);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(episode.state.mode),
                L"A current outward fragment must not discard the previously submitted Scope contract.");

            recovery.candidate=Evaluate(recovery.crop);
            Assert::IsTrue(recovery.candidate.applyCrop);
            const auto bounded=EvaluateNearBlackBoundedPresentation(episode.state,recovery);
            Assert::IsTrue(bounded.boundedPresentation,
                L"Use the exact measured extent as a temporary display envelope around the trusted Scope.");
            Assert::AreEqual(150,bounded.presentation.sourceBounds.top);
            Assert::AreEqual(1884,bounded.presentation.sourceBounds.bottom);
            Assert::AreEqual(0,bounded.presentation.sourceBounds.left);
            Assert::AreEqual(3840,bounded.presentation.sourceBounds.right);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(bounded.state.mode));
            Assert::AreEqual(276,bounded.state.entryTrustedCrop.top);
            Assert::IsFalse(bounded.state.boundedPresentationAvailable,
                L"Evaluation is a proposal; unsubmitted geometry must not be remembered as displayed.");
            const auto admitted=AdmitCropPresentation(recovery.previousAdmission,
                recovery.crop,bounded.presentation,recovery.presentationEpoch);
            Assert::IsFalse(admitted.blocked);
            Assert::AreEqual(276,admitted.state.trustedCrop.top);
            Assert::IsFalse(admitted.state.outwardPresentationAvailable);
            auto committed = CommitNearBlackForTest(bounded, bounded.state,
                admitted.state, recovery.crop.frameSourceSequence);
            Assert::IsTrue(committed.boundedPresentationAvailable);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP), int(committed.mode));
            Assert::AreEqual(276, committed.entryTrustedCrop.top);

            ++recovery.crop.frameSourceSequence;
            recovery.retentionSourceSequence=recovery.crop.frameSourceSequence;
            recovery.crop.currentVisibleBoundsAvailable=false;
            recovery.excludedBandsPixelSafe=true;
            recovery.observation=visible;
            recovery.candidate=Evaluate(recovery.crop);
            const auto uncertain=EvaluateNearBlackBoundedPresentation(committed,recovery);
            Assert::IsTrue(uncertain.boundedPresentation,
                L"When a sparse sample is missing, retain the last submitted complete envelope.");
            Assert::AreEqual(150,uncertain.presentation.sourceBounds.top);
            Assert::AreEqual(1884,uncertain.presentation.sourceBounds.bottom);
            const auto held = CommitNearBlackForTest(uncertain, committed,
                admitted.state, recovery.crop.frameSourceSequence);
            Assert::AreEqual(recovery.crop.frameSourceSequence,
                held.boundedPresentationPresentedSequence);
            Assert::AreEqual(276,held.entryTrustedCrop.top);
        }

        TEST_METHOD(RetainedScopeEnvelopeNeedsCurrentPixelsAndSelectedOutputCommit)
        {
            const ActivePictureBounds scope{30,276,3808,1884,3840,2160,
                3778.0/1608.0,ActivePictureBounds::BarAxes::BOTH};
            const ActivePictureBounds visible{0,150,3840,1884,3840,2160,
                3840.0/1734.0,ActivePictureBounds::BarAxes::NONE};
            auto input=ToyStoryNearBlackRecovery(5118);
            input.crop.geometry=scope;
            input.crop.currentVisibleBase=scope;
            input.crop.currentVisibleBounds=visible;
            input.crop.currentVisibleBoundsAvailable=true;
            input.crop.currentVisibleSourceGeneration=input.crop.frameSourceGeneration;
            input.crop.currentVisibleSourceSequence=input.crop.frameSourceSequence;
            input.crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.crop.geometrySourceGeneration=input.crop.frameSourceGeneration;
            input.crop.nearBlackEpisodeFullRaster=false;
            input.crop.nearBlackEpisodeRetainCrop=true;
            input.crop.latestObservationIsProvisional=true;
            input.crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            input.crop.latestObservationSupportsCrop=false;
            input.crop.verticalTranslationConfirmationPending=true;
            input.crop.verticalFitConfirmationPending=true;
            input.crop.verticalInspectionPending=true;
            input.observation=visible;
            input.observationClassification=ActivePictureClassification::PROVISIONAL;
            input.retentionBounds=scope;
            input.globalNearBlack=true;
            input.previousAdmission.trustedCrop=scope;
            NearBlackPresentationEpisodeState episode;
            episode.mode=NearBlackPresentationMode::RETAIN_CROP;
            episode.sourceGeneration=input.crop.frameSourceGeneration;
            episode.startedSourceSequence=5112;
            episode.presentationEpoch=input.presentationEpoch;
            episode.entryTrustedCropAvailable=true;
            episode.entryTrustedCrop=scope;
            episode.entryTrustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;

            const auto proposal=EvaluateNearBlackBoundedPresentation(episode,input);
            Assert::IsTrue(proposal.boundedPresentation,
                L"Exact current visible pixels can widen only presentation while subtitle inspection is pending.");
            Assert::AreEqual(150,proposal.presentation.sourceBounds.top);
            Assert::AreEqual(276,proposal.state.entryTrustedCrop.top);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(proposal.state.mode));
            Assert::IsFalse(proposal.state.boundedPresentationAvailable,
                L"The evaluator cannot claim that the proposed crop has already been shown.");

            auto commitInput = NearBlackBoundedPresentationCommitInput{};
            commitInput.currentEpisode=episode;
            commitInput.currentAdmission=input.previousAdmission;
            commitInput.proposalMode=proposal.proposalMode;
            commitInput.proposalBase=proposal.proposalBase;
            commitInput.proposalBounds=proposal.proposalBounds;
            commitInput.sourceGeneration=proposal.sourceGeneration;
            commitInput.presentationEpoch=proposal.presentationEpoch;
            commitInput.currentSourceSequence=input.crop.frameSourceSequence;
            commitInput.selectedSourceSequence=proposal.sourceSequence;
            commitInput.proposalSourceSequence=proposal.sourceSequence;
            commitInput.episodeStartSequence=proposal.episodeStartSequence;
            commitInput.selectedActualOwner=proposal.presentation.owner;
            commitInput.selectedActualBounds=proposal.presentation.sourceBounds;
            commitInput.renderedAndSubmitted=true;

            auto unsubmitted=commitInput;
            unsubmitted.renderedAndSubmitted=false;
            Assert::IsFalse(CommitNearBlackBoundedPresentation(unsubmitted).committed,
                L"A rendered-but-unsubmitted proposal cannot become temporal state.");
            auto wrongOwner=commitInput;
            wrongOwner.selectedActualOwner=DecisionOwner::TRUSTED_CROP;
            Assert::IsFalse(CommitNearBlackBoundedPresentation(wrongOwner).committed);
            auto wrongBounds=commitInput;
            wrongBounds.selectedActualBounds=scope;
            Assert::IsFalse(CommitNearBlackBoundedPresentation(wrongBounds).committed);
            auto afterCut=commitInput;
            afterCut.sceneBoundary=true;
            Assert::IsFalse(CommitNearBlackBoundedPresentation(afterCut).committed);

            const auto committed=CommitNearBlackBoundedPresentation(commitInput);
            Assert::IsTrue(committed.committed);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(committed.state.mode));
            Assert::AreEqual(276,committed.state.entryTrustedCrop.top);
            Assert::AreEqual(150,committed.state.boundedPresentation.top);
            Assert::AreEqual(input.crop.frameSourceSequence,
                committed.state.boundedPresentationPresentedSequence);
        }

        TEST_METHOD(RetainedScopeDoesNotRevertToFullRasterOnWeakDarkFrameEvidence)
        {
            const ActivePictureBounds scope{30,276,3808,1884,3840,2160,
                3778.0/1608.0,ActivePictureBounds::BarAxes::BOTH};
            const auto full=ActivePictureBounds{0,0,3840,2160,3840,2160,
                16.0/9.0,ActivePictureBounds::BarAxes::NONE};
            auto input=ToyStoryNearBlackRecovery(5118);
            input.crop.geometry=scope;
            input.crop.currentVisibleBase=scope;
            input.crop.currentVisibleBounds=full;
            input.crop.currentVisibleBoundsAvailable=true;
            input.crop.currentVisibleSourceGeneration=input.crop.frameSourceGeneration;
            input.crop.currentVisibleSourceSequence=input.crop.frameSourceSequence;
            input.crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.crop.geometrySourceGeneration=input.crop.frameSourceGeneration;
            input.crop.nearBlackEpisodeFullRaster=false;
            input.crop.nearBlackEpisodeRetainCrop=true;
            input.crop.latestObservationIsProvisional=true;
            input.crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            input.crop.latestObservationSupportsCrop=false;
            input.observation=full;
            input.observationClassification=ActivePictureClassification::PROVISIONAL;
            input.retentionBounds=scope;
            input.globalNearBlack=true;
            input.previousAdmission.trustedCrop=scope;
            NearBlackPresentationEpisodeState episode;
            episode.mode=NearBlackPresentationMode::RETAIN_CROP;
            episode.sourceGeneration=input.crop.frameSourceGeneration;
            episode.startedSourceSequence=5112;
            episode.presentationEpoch=input.presentationEpoch;
            episode.entryTrustedCropAvailable=true;
            episode.entryTrustedCrop=scope;
            episode.entryTrustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;

            // The generic recovery path has lost its crop. No current outward
            // pixel witness contradicts the last successfully submitted Scope.
            input.crop.currentVisibleBounds=scope;
            input.observation=scope;
            input.submittedScopeAnchor.crop=scope;
            input.candidate={};
            input.candidate.sourceBounds=full;
            input.candidate.applyCrop=false;
            input.candidate.owner=DecisionOwner::FULL_RASTER;
            const auto held=EvaluateNearBlackBoundedPresentation(episode,input);
            Assert::IsTrue(held.retainedScopeFallback);
            Assert::IsTrue(held.presentation.applyCrop);
            Assert::AreEqual(scope.left,held.presentation.sourceBounds.left);
            Assert::AreEqual(scope.top,held.presentation.sourceBounds.top);
            Assert::AreEqual(scope.right,held.presentation.sourceBounds.right);
            Assert::AreEqual(scope.bottom,held.presentation.sourceBounds.bottom);
            Assert::IsFalse(held.failOpenProposal);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(held.state.mode));
            Assert::AreEqual(276,held.state.entryTrustedCrop.top);

            // Sparse current occupancy is an acquisition veto, not proof that
            // the already displayed picture expanded to full raster.
            auto fullPixels=input;
            fullPixels.crop.currentVisibleBounds=full;
            fullPixels.observation=full;
            const auto fullOutput=EvaluateNearBlackBoundedPresentation(episode,fullPixels);
            Assert::IsTrue(fullOutput.retainedScopeFallback);
            Assert::IsTrue(fullOutput.presentation.applyCrop);
            Assert::AreEqual(scope.top,fullOutput.presentation.sourceBounds.top);
            Assert::AreEqual(scope.bottom,fullOutput.presentation.sourceBounds.bottom);

            auto lostWitness=held.state;
            lostWitness.boundedPresentationFailed=true;
            input.crop.currentVisibleBoundsAvailable=false;
            input.observationAvailable=false;
            input.retentionEvaluated=false; // Missing evidence, not an affirmative unsafe scan.
            const auto heldAfterFailure=EvaluateNearBlackBoundedPresentation(lostWitness,input);
            Assert::IsTrue(heldAfterFailure.retainedScopeFallback);
            Assert::IsTrue(heldAfterFailure.presentation.applyCrop);
            Assert::AreEqual(276,heldAfterFailure.presentation.sourceBounds.top);

            auto unsafeMissing=input;
            unsafeMissing.retentionEvaluated=true;
            unsafeMissing.excludedBandsPixelSafe=false;
            const auto unsafeOutput=EvaluateNearBlackBoundedPresentation(lostWitness,unsafeMissing);
            Assert::IsTrue(unsafeOutput.presentation.applyCrop,
                L"Unclassified unsafe pixels without an envelope cannot release submitted Scope.");
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(unsafeOutput.state.mode),
                L"A temporary safety output does not erase the logical picture contract.");
            fullPixels.candidate=held.presentation;
            const auto fullFromTight=EvaluateNearBlackBoundedPresentation(episode,fullPixels);
            Assert::IsTrue(fullFromTight.presentation.applyCrop,
                L"An unconfirmed sparse extent cannot replace the submitted Scope presentation.");

            // Explicit full-raster authority is allowed to release the Scope.
            input.crop.fullRasterPresentationAuthoritative=true;
            input.crop.latestObservationClassification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            input.observationClassification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            const auto authoritativeFull=EvaluateNearBlackBoundedPresentation(episode,input);
            Assert::IsFalse(authoritativeFull.retainedScopeFallback);
            Assert::IsFalse(authoritativeFull.presentation.applyCrop);
        }

        TEST_METHOD(SubmittedNativeScopeAnchorCommitsOnlySelectedSafePresentation)
        {
            const ActivePictureBounds scope{0,276,3840,1884,3840,2160,
                3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            SubmittedNativeScopeAnchorUpdateInput update;
            update.nativeCrop=scope;
            update.submittedBounds=scope;
            update.nativeCropAvailable=true;
            update.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            update.selectedOwner=DecisionOwner::TRUSTED_CROP;
            update.sourceGeneration=7;
            update.sourceSequence=100;
            update.presentationEpoch=9;

            auto beforeSubmit=CommitSubmittedNativeScopeAnchor(update);
            Assert::IsFalse(beforeSubmit.available,
                L"Pre-render admission must not masquerade as an actually shown Scope.");
            update.frameSubmitted=true;
            auto shown=CommitSubmittedNativeScopeAnchor(update);
            Assert::IsTrue(IsCurrentSubmittedNativeScopeAnchor(shown,7,100,9,3840,2160));
            Assert::AreEqual(100ull,shown.lastSubmittedSourceSequence);

            update.previous=shown;
            update.selectedOwner=DecisionOwner::FULL_RASTER;
            auto weakFullRaster=CommitSubmittedNativeScopeAnchor(update);
            Assert::IsTrue(weakFullRaster.available,
                L"An unconfirmed full-raster fallback must not erase the last submitted Scope anchor.");
            update.fullRasterAuthority=true;
            auto authoritativeFull=CommitSubmittedNativeScopeAnchor(update);
            Assert::IsFalse(authoritativeFull.available,
                L"Confirmed full-raster authority must retire the prior Scope anchor.");

            update.fullRasterAuthority=false;
            update.selectedOwner=DecisionOwner::TRUSTED_CROP;
            update.sourceSequence=101;
            update.submittedBounds=scope;
            update.nativeCrop=scope;
            update.previous=shown;
            update.presentationEpoch=10;
            auto newEpoch=CommitSubmittedNativeScopeAnchor(update);
            Assert::IsTrue(newEpoch.available,
                L"A successfully submitted native Scope establishes a fresh anchor in the new epoch.");
            Assert::IsTrue(IsCurrentSubmittedNativeScopeAnchor(newEpoch,7,101,10,3840,2160));
            Assert::IsFalse(IsCurrentSubmittedNativeScopeAnchor(shown,7,101,10,3840,2160),
                L"The prior epoch anchor is not current in the new viewport.");
        }

        TEST_METHOD(SelectedAspectFillPreservesNativeScopeWhenSubtitleAppears)
        {
            // Recording 10:41:03-10:41:04: native 2.382 Scope was rendered
            // with the user's 2.35 screen fill, then caption pixels appeared.
            const ActivePictureBounds scope{0,272,3840,1884,3840,2160,
                3840.0/1612.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            auto filled=scope;filled.left=26;filled.right=3814;
            filled.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            SubmittedNativeScopeAnchorUpdateInput u;
            u.nativeCrop=scope;u.submittedBounds=filled;
            u.nativeCropAvailable=u.frameSubmitted=true;
            u.selectedOwner=DecisionOwner::TRUSTED_CROP;
            u.sourceGeneration=3;u.sourceSequence=19301;u.presentationEpoch=1;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available,
                L"An unexplained crop must not certify the physical picture.");
            auto& fill=u.selectedAdmission;
            fill.available=fill.optionalFillApplied=true;fill.trustedCrop=scope;
            fill.sourceGeneration=3;fill.presentationEpoch=1;
            fill.optionalFillSourceSequence=19301;fill.optionalFillSourceBounds=filled;
            const auto anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(anchor.available,L"Selected configured fill must retain the native Scope anchor.");
            Assert::AreEqual(0,anchor.crop.left);
            Assert::AreEqual(3840,anchor.crop.right);

            NearBlackPresentationEpisodeInput episode;
            episode.sourceGeneration=3;episode.sourceSequence=19302;episode.presentationEpoch=1;
            episode.measurementCurrent=episode.nearBlackEvaluated=episode.globalNearBlack=true;
            episode.trustedCropAvailable=true;episode.trustedCrop=scope;
            episode.previouslyAdmittedCropAvailable=anchor.available;
            episode.previouslyAdmittedCrop=anchor.crop;
            episode.previouslyAdmittedCropSourceGeneration=anchor.sourceGeneration;
            episode.previouslyAdmittedCropPresentationEpoch=anchor.presentationEpoch;
            episode.previous.mode=NearBlackPresentationMode::RETAIN_CROP;
            episode.previous.sourceGeneration=3;episode.previous.presentationEpoch=1;
            episode.previous.startedSourceSequence=19278;
            episode.previous.entryTrustedCropAvailable=true;episode.previous.entryTrustedCrop=scope;
            episode.boundedVisibleContentOutsideCrop=true;episode.retentionSafe=false;
            const auto caption=EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(caption.state.mode),
                L"Bounded caption pixels must not erase a physically presented Scope contract.");
            Assert::IsFalse(caption.changedToFullRaster);

            // Raw occupied bars cannot enlarge an already submitted Scope.
            // Only a separately confirmed current caption/FIT owner may expose
            // its bounded envelope without replacing the native AR contract.
            auto pending=ToyStoryNearBlackRecovery(19302);
            pending.presentationEpoch=1;pending.crop.framePresentationEpoch=1;
            pending.crop.frameSourceGeneration=pending.crop.geometrySourceGeneration=3;
            pending.crop.geometry=scope;pending.crop.currentVisibleBase=scope;
            pending.crop.currentVisibleSourceGeneration=3;
            pending.crop.currentVisibleBounds=scope;
            pending.crop.currentVisibleBounds.top=268;pending.crop.currentVisibleBounds.bottom=2024;
            pending.crop.nearBlackEpisodeFullRaster=false;pending.crop.nearBlackEpisodeRetainCrop=true;
            pending.crop.verticalTranslationConfirmationPending=true;
            pending.retentionBounds=scope;pending.retentionSourceGeneration=3;
            pending.observation=scope;pending.observation.top=268;pending.observation.bottom=1892;
            pending.previousAdmission=fill;pending.submittedScopeAnchor=anchor;
            pending.globalNearBlack=true;pending.candidate=Evaluate(pending.crop);
            const auto raw=EvaluateNearBlackBoundedPresentation(caption.state,pending);
            const auto rawAdmitted=AdmitCropPresentation(fill,pending.crop,raw.presentation,1);
            Assert::IsFalse(rawAdmitted.blocked);
            Assert::IsTrue(rawAdmitted.presentation.applyCrop);
            Assert::AreEqual(scope.top,rawAdmitted.presentation.sourceBounds.top,
                L"Pending raw bar occupancy must retain the submitted native crop.");
            Assert::AreEqual(scope.bottom,rawAdmitted.presentation.sourceBounds.bottom);
            Assert::IsFalse(raw.boundedPresentation);
            Assert::IsFalse(anchor.submittedEnvelopeAvailable);

            // Explicit current caption ownership originates the positive case;
            // recovery's raw visible rectangle cannot supply that certificate.
            pending.measurementCurrent=pending.retentionEvaluated=true;
            pending.retentionSourceSequence=pending.crop.frameSourceSequence;
            pending.confirmedPresentationResolved=true;
            pending.crop.outwardPresentationActive=pending.crop.outwardExpansionAvailable=true;
            pending.crop.outwardExpansion=pending.crop.currentVisibleBounds;
            pending.crop.outwardExpansion.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            pending.crop.outwardExpansion.aspectRatio=3840.0/(2024-268);
            pending.crop.outwardExpansionSourceGeneration=3;
            auto captionOwner=pending.crop;
            captionOwner.nearBlackEpisodeRetainCrop=captionOwner.nearBlackEpisodeFullRaster=false;
            pending.candidate=Evaluate(captionOwner);
            Assert::IsTrue(pending.candidate.applyCrop);
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),int(pending.candidate.owner));
            QualifyExplicitCaptionFitFixture(pending,pending.crop.outwardExpansion);
            const auto bounded=EvaluateNearBlackBoundedPresentation(caption.state,pending);
            const auto admitted=AdmitCropPresentation(fill,pending.crop,bounded.presentation,1);
            Assert::IsFalse(admitted.blocked);
            Assert::IsTrue(admitted.presentation.applyCrop);
            Assert::IsTrue(admitted.presentation.sourceBounds.top<=268);
            Assert::IsTrue(admitted.presentation.sourceBounds.bottom>=2024);
            Assert::IsTrue(admitted.presentation.sourceBounds.top>0 &&
                admitted.presentation.sourceBounds.bottom<2160,
                L"Confirmed caption preserves logical Scope and its bounded owned envelope.");
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),int(admitted.presentation.owner));
            Assert::AreEqual(scope.top,admitted.state.trustedCrop.top);
            Assert::AreEqual(scope.bottom,admitted.state.trustedCrop.bottom);
            SubmittedNativeScopeAnchorUpdateInput captionSubmission;
            captionSubmission.previous=anchor;captionSubmission.selectedAdmission=admitted.state;
            captionSubmission.nativeCropAvailable=captionSubmission.frameSubmitted=true;
            captionSubmission.nativeCrop=scope;
            captionSubmission.submittedBounds=admitted.presentation.sourceBounds;
            captionSubmission.selectedOwner=admitted.presentation.owner;
            captionSubmission.sourceGeneration=3;captionSubmission.sourceSequence=19302;
            captionSubmission.presentationEpoch=1;
            const auto captionShown=CommitSubmittedNativeScopeAnchor(captionSubmission);
            Assert::IsTrue(captionShown.submittedEnvelopeAvailable);
            Assert::AreEqual(268,captionShown.submittedEnvelope.top);
            Assert::AreEqual(2024,captionShown.submittedEnvelope.bottom);
            Assert::AreEqual(anchor.lastSubmittedSourceSequence,captionShown.lastSubmittedSourceSequence,
                L"Submitting a caption FIT must not renew the native Scope watermark.");

            // A selected output is allowed to lag live processing, but its fill
            // witness must belong to that selected frame, not the newest one.
            fill.optionalFillSourceSequence=19302;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available);
            fill.optionalFillSourceSequence=19301;fill.presentationEpoch=2;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available);
            fill.presentationEpoch=1;fill.sourceGeneration=4;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available);
            fill.sourceGeneration=3;u.frameSubmitted=false;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available);
            u.frameSubmitted=true;u.submittedBounds.top+=20;
            fill.optionalFillSourceBounds=u.submittedBounds;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available,
                L"Horizontal fill cannot explain vertical clipping.");
            u.submittedBounds=scope;u.submittedBounds.top+=34;u.submittedBounds.bottom+=34;
            fill.optionalFillApplied=false;u.selectedOwner=DecisionOwner::VERTICAL_TRANSLATION;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available,
                L"An equal-aspect shifted source rectangle is not the submitted native base.");
            u.previous=anchor;u.sourceSequence=19302;
            const auto unchanged=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(unchanged.available);
            Assert::AreEqual(uint64_t(19301),unchanged.lastSubmittedSourceSequence);
            u.previous={};u.sourceSequence=19301;u.submittedBounds=scope;
            Assert::IsTrue(CommitSubmittedNativeScopeAnchor(u).available,
                L"Subtitle layout translation may still submit the exact native source crop.");
            u.submittedBounds=filled;fill.optionalFillSourceBounds=filled;
            fill.optionalFillApplied=true;
            u.fullRasterAuthority=true;u.previous=anchor;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available,
                L"Real full-raster authority still retires Scope.");
        }

        TEST_METHOD(SubmittedScopeContinuityRequiresCurrentSafePixelsOrTheExactRepeatedFrame)
        {
            const ActivePictureBounds scope{0,276,3840,1884,3840,2160,
                3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            SubmittedScopeContinuityEvidence evidence;
            evidence.anchor.available=true;
            evidence.anchor.crop=scope;
            evidence.anchor.sourceGeneration=7;
            evidence.anchor.presentationEpoch=9;
            evidence.anchor.lastSubmittedSourceSequence=100;
            evidence.retentionBounds=scope;
            evidence.sourceGeneration=7;
            evidence.sourceSequence=101;
            evidence.presentationEpoch=9;
            evidence.retentionSourceGeneration=7;
            evidence.retentionSourceSequence=101;
            evidence.rasterWidth=3840;
            evidence.rasterHeight=2160;
            evidence.measurementCurrent=true;
            evidence.retentionEvaluated=true;
            evidence.retentionSafe=true;
            evidence.excludedBandsPixelSafe=true;
            Assert::IsTrue(CanRetainSubmittedScopeContinuity(evidence),
                L"Fresh clean-band evidence may retain the previously submitted native Scope.");

            evidence.currentClassification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            Assert::IsFalse(CanRetainSubmittedScopeContinuity(evidence),
                L"Current trusted full-raster evidence must release the old Scope.");
            evidence.currentClassification=ActivePictureClassification::UNAVAILABLE;
            evidence.fullRasterAuthority=true;
            Assert::IsFalse(CanRetainSubmittedScopeContinuity(evidence));
            evidence.fullRasterAuthority=false;
            evidence.outwardVisibleBoundsAvailable=true;
            Assert::IsFalse(CanRetainSubmittedScopeContinuity(evidence),
                L"Current outward pixels cannot be hidden by stale Scope edges.");
            evidence.outwardVisibleBoundsAvailable=false;
            evidence.excludedBandsPixelSafe=false;
            Assert::IsFalse(CanRetainSubmittedScopeContinuity(evidence),
                L"Unsafe excluded bands veto continuity.");

            evidence.cadenceRepeat=true;
            evidence.sourceSequence=100;
            evidence.measurementCurrent=false;
            evidence.retentionEvaluated=false;
            evidence.retentionSafe=false;
            evidence.excludedBandsPixelSafe=false;
            Assert::IsTrue(CanRetainSubmittedScopeContinuity(evidence),
                L"An identical already-submitted source frame may be repeated without rescanning.");
            evidence.sourceSequence=101;
            Assert::IsFalse(CanRetainSubmittedScopeContinuity(evidence),
                L"A repeat marker cannot retain a different source frame without fresh evidence.");
        }

        TEST_METHOD(SubmittedScopeContinuityIsPresentationRetentionNotNewAuthority)
        {
            const ActivePictureBounds scope{0,276,3840,1884,3840,2160,
                3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            Input input;
            input.automaticCropEnabled=true;
            input.sharedGeometryAvailable=true;
            input.geometry=scope;
            input.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.geometrySourceGeneration=7;
            input.frameSourceGeneration=7;
            input.frameSourceSequence=101;
            input.framePresentationEpoch=9;
            input.rasterWidth=3840;
            input.rasterHeight=2160;
            input.latestObservationIsUnavailable=true;
            input.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
            input.submittedScopeContinuityRetention=true;
            input.nearBlackEpisodeFullRaster=true;
            const auto retained=Evaluate(input);
            Assert::IsTrue(retained.applyCrop);
            Assert::AreEqual(int(DecisionOwner::SUBMITTED_SCOPE_RETENTION),int(retained.owner));
            Assert::AreEqual(scope.top,retained.sourceBounds.top);

            input.movingPictureTransition=true;
            const auto transitionRetained=Evaluate(input);
            Assert::IsTrue(transitionRetained.applyCrop,
                L"A scene or motion transition alone cannot erase a submitted Scope when current pixels prove its bars safe.");
            input.fullRasterPresentationAuthoritative=true;
            const auto authoritativeFull=Evaluate(input);
            Assert::IsFalse(authoritativeFull.applyCrop,
                L"Current full-raster authority still overrides submitted Scope continuity.");
        }

        TEST_METHOD(SubmittedScopeContinuityClearsAnAmbiguousFullRasterRecoveryFallback)
        {
            const ActivePictureBounds scope{0,276,3840,1884,3840,2160,
                3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            Input crop;
            crop.automaticCropEnabled=true;
            crop.sharedGeometryAvailable=true;
            crop.geometry=scope;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
            crop.frameSourceSequence=101;
            crop.framePresentationEpoch=9;
            crop.rasterWidth=3840;
            crop.rasterHeight=2160;
            crop.latestObservationIsUnavailable=true;
            crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
            crop.submittedScopeContinuityRetention=true;
            crop.nearBlackEpisodeFullRaster=true;
            crop.movingPictureTransition=true;

            PresentationRecoveryInput input;
            input.crop=crop;
            input.candidate=Evaluate(crop);
            input.previous.active=true;
            input.previous.trustedCrop=scope;
            input.previous.sourceGeneration=7;
            input.previous.presentationEpoch=9;
            input.previous.startedSourceSequence=90;
            input.previous.fallbackBoundsAvailable=true;
            input.previous.fallbackBounds={0,0,3840,2160,3840,2160,16.0/9.0,
                ActivePictureBounds::BarAxes::NONE};
            input.presentationEpoch=9;
            input.framesPerSecond=24;

            const auto recovered=EvaluatePresentationRecovery(input);
            Assert::IsTrue(recovered.presentation.applyCrop,
                L"Current safe Scope evidence must replace an ambiguous old full-raster fallback.");
            Assert::AreEqual(276,recovered.presentation.sourceBounds.top);
            Assert::IsFalse(recovered.state.active,
                L"Resolved continuity must clear stale recovery state so it cannot overwrite Scope again.");
        }

        TEST_METHOD(NearBlackEpisodeUsesSubmittedScopeAcrossGeometryLossAndUnconfirmedFullRaster)
        {
            const ActivePictureBounds scope{0,276,3840,1884,3840,2160,
                3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            NearBlackPresentationEpisodeInput input;
            input.measurementCurrent=input.nearBlackEvaluated=input.globalNearBlack=true;
            input.sourceGeneration=7;
            input.sourceSequence=200;
            input.presentationEpoch=9;
            input.previouslyAdmittedCropAvailable=true;
            input.previouslyAdmittedCrop=scope;
            input.previouslyAdmittedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;
            input.previouslyAdmittedCropSourceGeneration=7;
            input.previouslyAdmittedCropPresentationEpoch=9;
            auto first=EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(first.state.mode));
            Assert::IsTrue(first.state.entryTrustedCropAvailable);
            Assert::AreEqual(scope.top,first.state.entryTrustedCrop.top);

            input.previous=first.state;
            input.previous.mode=NearBlackPresentationMode::FULL_RASTER;
            input.previous.startedAtFullRaster=true;
            input.previous.fullRasterStartedSourceSequence=201;
            input.previous.startedSourceSequence=190;
            input.sourceSequence=202;
            const auto restored=EvaluateNearBlackPresentationEpisode(input);
            Assert::IsTrue(restored.restoredSubmittedScope);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(restored.state.mode));
            Assert::AreEqual(scope.top,restored.state.entryTrustedCrop.top);

            input.previous=restored.state;
            input.previous.mode=NearBlackPresentationMode::FULL_RASTER;
            input.previous.startedAtFullRaster=true;
            input.previous.fullRasterStartedSourceSequence=202;
            input.fullRasterAuthorityAvailable=true;
            const auto authoritative=EvaluateNearBlackPresentationEpisode(input);
            Assert::IsFalse(authoritative.restoredSubmittedScope);
            Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),int(authoritative.state.mode));
        }

        TEST_METHOD(NearBlackBoundedReplayKeepsPriorScopeWhenDarkFrameHasOutwardPixels)
        {
            auto input = ToyStoryNearBlackRecovery(5118);
            auto& crop = input.crop;
            const ActivePictureBounds scope{ 30, 276, 3808, 1884, 3840, 2160,
                3778.0 / 1608.0, ActivePictureBounds::BarAxes::BOTH };
            crop.geometry = scope;
            crop.currentVisibleBase = scope;
            crop.currentVisibleBounds = { 0, 150, 3840, 1884, 3840, 2160,
                3840.0 / 1734.0, ActivePictureBounds::BarAxes::NONE };
            input.retentionBounds = scope;
            input.observation = crop.currentVisibleBounds;
            input.previousAdmission.trustedCrop = scope;

            auto episode = ToyStoryRetainedEpisode(input);
            episode.mode = NearBlackPresentationMode::RETAIN_CROP;
            episode.startedAtFullRaster = false;
            episode.startedSourceSequence = input.crop.frameSourceSequence - 6;
            episode.fullRasterStartedSourceSequence = 0;
            episode.sourceGeneration = input.crop.frameSourceGeneration;
            episode.presentationEpoch = input.presentationEpoch;
            episode.entryTrustedCropAvailable = true;
            episode.entryTrustedCrop = scope;
            episode.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
            input.crop.nearBlackEpisodeFullRaster = false;
            input.crop.nearBlackEpisodeRetainCrop = true;
            input.globalNearBlack = true;

            const auto bounded = EvaluateNearBlackBoundedPresentation(episode, input);
            Assert::IsTrue(bounded.boundedPresentation,
                L"The logged dark Scope frame has an exact visible extent above its already-admitted crop; it should show the bounded union, not full raster.");
            Assert::IsTrue(bounded.presentation.applyCrop);
            Assert::AreEqual(150, bounded.presentation.sourceBounds.top);
            Assert::AreEqual(1884, bounded.presentation.sourceBounds.bottom);
            Assert::AreEqual(0, bounded.presentation.sourceBounds.left);
            Assert::AreEqual(3840, bounded.presentation.sourceBounds.right);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
                int(bounded.state.mode));
            Assert::AreEqual(276, bounded.state.entryTrustedCrop.top,
                L"The existing Scope remains the logical AR anchor.");
            Assert::AreEqual(276, episode.entryTrustedCrop.top);

            const auto admitted = AdmitCropPresentation(input.previousAdmission,
                input.crop, bounded.presentation, input.presentationEpoch);
            Assert::IsFalse(admitted.blocked);
            Assert::AreEqual(276, admitted.state.trustedCrop.top);
            Assert::IsFalse(admitted.state.outwardPresentationAvailable,
                L"The temporary visibility envelope must not become a persistent generic-FIT owner.");
            const auto committed = CommitNearBlackForTest(bounded, episode,
                admitted.state, input.crop.frameSourceSequence);
            Assert::IsTrue(committed.boundedPresentationAvailable);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP), int(committed.mode));
            Assert::AreEqual(276, committed.entryTrustedCrop.top);

            input.cadenceRepeat = true;
            const auto repeated = EvaluateNearBlackBoundedPresentation(
                committed, input);
            Assert::IsTrue(repeated.boundedPresentation,
                L"An identical paused dark source may reuse its exact already-certified outward envelope.");
            Assert::AreEqual(150, repeated.presentation.sourceBounds.top);
        }

        TEST_METHOD(NearBlackBoundedReplayHoldsEnvelopeAndExposesFurtherGrowth)
        {
            auto input = ToyStoryNearBlackRecovery();
            auto state = ToyStoryRetainedEpisode(input);
            for (int i = 0; i < 7; ++i)
            {
                input.crop.frameSourceSequence = input.retentionSourceSequence =
                    input.crop.currentVisibleSourceSequence = 30120 + i;
                input.crop.currentVisibleBounds.top = i % 2 ? 29 : 27;
                auto result = EvaluateNearBlackBoundedPresentation(state, input);
                Assert::IsTrue(result.presentation.applyCrop);
                Assert::AreEqual(26, result.presentation.sourceBounds.top);
                Assert::AreEqual(0u, result.state.revalidationSamples);
                state = CommitNearBlackForTest(result, state,
                    input.previousAdmission, input.crop.frameSourceSequence);
            }
            ++input.crop.frameSourceSequence; ++input.retentionSourceSequence;
            ++input.crop.currentVisibleSourceSequence;
            input.crop.currentVisibleBounds.top = 11;
            auto grown = EvaluateNearBlackBoundedPresentation(state, input);
            Assert::AreEqual(10, grown.presentation.sourceBounds.top);
            Assert::IsTrue(grown.presentation.applyCrop);
        }
        TEST_METHOD(NearBlackBoundedReplayRejectsStaleUnsafeAndCompetingEvidence)
        {
            for (int failure = 0; failure < 37; ++failure)
            {
                auto input = ToyStoryNearBlackRecovery();
                auto state = ToyStoryRetainedEpisode(input);
                switch (failure) {
                case 0: input.measurementCurrent = false; break;
                case 1: input.retentionEvaluated = false; break;
                case 2: --input.retentionSourceSequence; break;
                case 3: --input.retentionSourceGeneration; break;
                case 4: input.retentionBounds.top += 2; break;
                case 5: input.crop.currentVisibleBoundsAvailable = false; break;
                case 6: --input.crop.currentVisibleSourceSequence; break;
                case 7: --input.crop.currentVisibleSourceGeneration; break;
                case 8: input.crop.currentVisibleBase.top += 2; break;
                case 9: input.crop.currentVisibleBounds.top = 43; break; // Does not cover observed row 40.
                case 10:
                    input.globalNearBlack = true;
                    input.crop.currentVisibleBounds = input.crop.geometry;
                    input.observation = input.crop.geometry;
                    break;
                case 11: input.nearBlackEvaluated = false; break;
                case 12: input.observationAvailable = false; break;
                case 13: input.crop.fullRasterPresentationAuthoritative = true; break;
                case 14: input.crop.movingPictureTransition = true; break;
                case 15: input.crop.verticalTranslationActive = true; break;
                case 16: input.crop.presentationFailOpen = true; break;
                case 17: input.previousAdmission.available = false; break;
                case 18: --input.presentationEpoch; break;
                case 19:
                    state.startedAtFullRaster = true;
                    input.globalNearBlack = true;
                    input.crop.currentVisibleBounds = input.crop.geometry;
                    input.observation = input.crop.geometry;
                    break;
                case 20: state.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT; break;
                case 21: input.crop.automaticCropEnabled = false; break;
                case 22: input.crop.verticalTranslationConfirmationPending = true; break;
                case 23: input.crop.latestObservationIsUnavailable = true; break;
                case 24: input.crop.currentVisibleBounds.bottom = 2162; break;
                case 25: --input.previousAdmission.sourceGeneration; break;
                case 26: --input.previousAdmission.presentationEpoch; break;
                case 27: input.previousAdmission.trustedCrop.top += 2; break;
                case 28: input.previous.active = true; break;
                case 29: input.crop.verticalFitConfirmationPending = true; break;
                case 30: input.crop.verticalInspectionPending = true; break;
                case 31: input.crop.latestObservationClassification = ActivePictureClassification::FULL_RASTER_TRUSTED; break;
                case 32: input.observationClassification = ActivePictureClassification::FULL_RASTER_TRUSTED; break;
                case 33: --state.sourceGeneration; break;
                case 34: state.entryTrustedCrop.top += 2; break;
                case 35: --input.crop.geometrySourceGeneration; break;
                case 36: input.crop.sharedGeometryAvailable = false; break;
                }
                AssertFullRaster(EvaluateNearBlackBoundedPresentation(state, input).presentation);
            }
        }
        TEST_METHOD(NearBlackBoundedReplayReturnsOnlyAfterUnchangedSevenFrameProof)
        {
            auto input = ToyStoryNearBlackRecovery();
            const auto initial = EvaluateNearBlackBoundedPresentation(ToyStoryRetainedEpisode(input), input);
            auto state = CommitNearBlackForTest(initial,initial.state,input.previousAdmission,input.crop.frameSourceSequence);
            NearBlackPresentationEpisodeInput proof;
            proof.measurementCurrent = proof.nearBlackEvaluated = proof.trustedCropAvailable = true;
            proof.trustedCrop = input.crop.geometry;
            proof.sourceGeneration = input.crop.frameSourceGeneration;
            proof.presentationEpoch = input.presentationEpoch;
            proof.framesPerSecond = 24;
            proof.currentObservationAvailable = proof.retentionEvaluated = proof.retentionSafe = true;
            proof.retentionExcludedBandsPixelSafe = true;
            proof.currentObservation = proof.retentionBounds = input.crop.geometry;
            proof.currentObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            proof.retentionSourceGeneration = proof.sourceGeneration;
            proof.knownTrustedGeometryReacquired = proof.reacquisitionIsCurrentAssociation = true;
            proof.reacquiredTrustedGeometry = proof.trustedCrop;
            proof.reacquiredTrustedClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            proof.reacquiredSourceGeneration = proof.sourceGeneration;
            proof.reacquiredPresentationEpoch = proof.presentationEpoch;
            for (uint64_t i = 1; i <= 7; ++i)
            {
                proof.previous = state;
                proof.sourceSequence = proof.retentionSourceSequence = proof.reacquiredSourceSequence = 30120+i;
                const auto episode = EvaluateNearBlackPresentationEpisode(proof);
                Assert::AreEqual(i == 7, episode.releasedToTrustedCrop);
                input.crop.frameSourceSequence = input.retentionSourceSequence = 30120+i;
                input.crop.currentVisibleBoundsAvailable = false;
                input.excludedBandsPixelSafe = true;
                input.observation = input.crop.geometry;
                input.crop.latestObservationSupportsCrop = true;
                input.crop.nearBlackEpisodeFullRaster = episode.state.mode == NearBlackPresentationMode::FULL_RASTER;
                input.candidate = Evaluate(input.crop);
                const auto presentation = EvaluateNearBlackBoundedPresentation(episode.state, input);
                Assert::IsTrue(presentation.presentation.applyCrop);
                Assert::AreEqual(i == 7 ? 42 : 26, presentation.presentation.sourceBounds.top);
                Assert::AreEqual(episode.state.revalidationSamples, presentation.state.revalidationSamples);
                state = CommitNearBlackForTest(presentation,episode.state,input.previousAdmission,input.crop.frameSourceSequence);
            }
        }
        TEST_METHOD(NearBlackBoundedReplayCannotAcquireOnRepeatButPreservesCertifiedRepeat)
        {
            auto input = ToyStoryNearBlackRecovery();
            const auto episode = ToyStoryRetainedEpisode(input);
            input.cadenceRepeat = true;
            AssertFullRaster(EvaluateNearBlackBoundedPresentation(episode, input).presentation);
            input.cadenceRepeat = false;
            auto first = EvaluateNearBlackBoundedPresentation(episode, input);
            Assert::IsTrue(first.boundedPresentation);
            const auto committed = CommitNearBlackForTest(first, episode,
                input.previousAdmission, input.crop.frameSourceSequence);
            Assert::IsTrue(committed.boundedPresentationAvailable);
            input.cadenceRepeat = true;
            auto repeat = EvaluateNearBlackBoundedPresentation(committed, input);
            Assert::IsTrue(repeat.boundedPresentation);
            Assert::AreEqual(26, repeat.presentation.sourceBounds.top);
            Assert::AreEqual(first.state.revalidationSamples, repeat.state.revalidationSamples);
        }
        TEST_METHOD(NearBlackBoundedReplayUsesRealP010AndV210ExtentEvidence)
        {
            const int width = 384, height = 216;
            std::vector<uint16_t> planar(width*height*3/2, uint16_t(512<<6));
            std::vector<uint32_t> packedPixels(width/6*4*height);
            for (int y=0; y<height; ++y)
            {
                const uint32_t level = y<26 || y>=188 ? 64 : y<28 ? 103 : 281;
                for (int x=0; x<width; ++x) planar[y*width+x] = uint16_t(level<<6);
                for (int x=0; x<width; x+=6)
                {
                    auto words = packedPixels.data()+y*width/6*4+x/6*4;
                    words[0]=512|(level<<10)|(512<<20);
                    words[1]=level|(512<<10)|(level<<20);
                    words[2]=512|(level<<10)|(512<<20);
                    words[3]=level|(512<<10)|(level<<20);
                }
            }
            for (bool packed : {false,true})
            {
                AnalysisLumaSource source{reinterpret_cast<const uint8_t*>(planar.data()),
                    planar.size()*sizeof(uint16_t),width,height,width*2,width*2,
                    AnalysisLumaFormat::P010,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                if (packed) {
                    source.data=reinterpret_cast<const uint8_t*>(packedPixels.data());
                    source.dataBytes=packedPixels.size()*sizeof(uint32_t);
                    source.rowBytes=width/6*16; source.chromaRowBytes=0;
                    source.format=AnalysisLumaFormat::NativeYuv422;
                }
                auto input=ToyStoryNearBlackRecovery();
                auto& crop=input.crop;
                crop.rasterWidth=width; crop.rasterHeight=height;
                crop.geometry={0,28,width,188,width,height,384.0/160.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                const auto pixels=EvaluateActivePicturePresentationRetention(source,crop.geometry);
                Assert::IsTrue(pixels.analysisValid && pixels.presentationValid && pixels.outwardVisibleBoundsAvailable);
                Assert::IsFalse(pixels.excludedBandsPixelSafe);
                input.retentionBounds=crop.currentVisibleBase=input.previousAdmission.trustedCrop=crop.geometry;
                crop.currentVisibleBounds=pixels.outwardVisibleBounds;
                input.observation=pixels.activePicture.proposedBounds;
                input.observationClassification=crop.latestObservationClassification=pixels.activePicture.classification;
                const auto coarse=ResolvePresentationObservation(crop.geometry,pixels.activePicture,pixels);
                crop.currentVisibleBounds.top=std::min(crop.currentVisibleBounds.top,coarse.bounds.top);
                crop.currentVisibleBounds.bottom=std::max(crop.currentVisibleBounds.bottom,coarse.bounds.bottom);
                input.candidate=Evaluate(crop);
                auto result=EvaluateNearBlackBoundedPresentation(ToyStoryRetainedEpisode(input),input);
                Assert::IsTrue(result.boundedPresentation);
                Assert::IsTrue(result.presentation.sourceBounds.top <= 26);
                Assert::AreEqual(188,result.presentation.sourceBounds.bottom);
                Assert::IsTrue(result.presentation.sourceBounds.top > 0);
            }
        }
        TEST_METHOD(NearBlackBoundedReplayAcceptsExistingAcquisitionSuppressionWithoutNewAuthority)
        {
            auto input=ToyStoryNearBlackRecovery();
            ActivePictureEvidence raw;
            raw.available=true;
            raw.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            raw.proposedBounds=raw.trustedBounds=input.observation;
            const auto constrained=ConstrainNearBlackCropAcquisition(raw,true);
            Assert::IsTrue(constrained.classification == ActivePictureClassification::PROVISIONAL);
            input.crop.latestObservationClassification=constrained.classification;
            input.crop.latestObservationIsProvisional=true;
            input.crop.latestObservationSupportsCrop=false;
            const auto episode=ToyStoryRetainedEpisode(input);
            const auto generic=EvaluatePresentationRecovery(input);
            Assert::IsFalse(generic.state.active);
            input.previous=generic.state;
            input.candidate=generic.presentation;
            const auto result=EvaluateNearBlackBoundedPresentation(episode,input);
            Assert::IsTrue(result.boundedPresentation);
            Assert::AreEqual(26,result.presentation.sourceBounds.top);
            Assert::AreEqual(42,result.state.entryTrustedCrop.top);
            Assert::IsFalse(AdmitCropPresentation(input.previousAdmission,input.crop,
                result.presentation,input.presentationEpoch).blocked);
        }
        TEST_METHOD(NearBlackBoundedReplayGenuineRasterExpansionAlwaysWins)
        {
            auto input=ToyStoryNearBlackRecovery();
            auto state=EvaluateNearBlackBoundedPresentation(ToyStoryRetainedEpisode(input),input).state;
            ++input.crop.frameSourceSequence; ++input.retentionSourceSequence;
            ++input.crop.currentVisibleSourceSequence;
            input.crop.currentVisibleBounds.top=0;
            input.crop.currentVisibleBounds.bottom=2160;
            input.observation=input.crop.currentVisibleBounds;
            const auto result=EvaluateNearBlackBoundedPresentation(state,input);
            AssertFullRaster(result.presentation);
            Assert::IsFalse(result.state.boundedPresentationFailed,
                L"Evaluation without a selected output cannot latch presentation failure state.");
        }
        TEST_METHOD(NearBlackBoundedReplayMissingProofDoesNotLatchAnUnsubmittedFailure)
        {
            auto input = ToyStoryNearBlackRecovery();
            auto state = EvaluateNearBlackBoundedPresentation(ToyStoryRetainedEpisode(input), input).state;
            ++input.crop.frameSourceSequence; ++input.retentionSourceSequence;
            ++input.crop.currentVisibleSourceSequence;
            input.crop.currentVisibleBoundsAvailable = false;
            auto lost = EvaluateNearBlackBoundedPresentation(state, input);
            AssertFullRaster(lost.presentation);
            ++input.crop.frameSourceSequence; ++input.retentionSourceSequence;
            ++input.crop.currentVisibleSourceSequence;
            input.crop.currentVisibleBoundsAvailable = true;
            Assert::IsFalse(lost.state.boundedPresentationFailed,
                L"A scan with no submitted output cannot latch a display failure.");
            const auto recovered=EvaluateNearBlackBoundedPresentation(lost.state,input);
            Assert::IsTrue(recovered.proposalAvailable && recovered.presentation.applyCrop);
            Assert::AreEqual(26,recovered.presentation.sourceBounds.top);
            --input.crop.currentVisibleSourceSequence;
            AssertFullRaster(EvaluateNearBlackBoundedPresentation(lost.state,input).presentation);
        }
		TEST_METHOD(BoundedRecoveryAgencyPreservesBlackTopThroughExpiredInspection)
		{
			auto input = AgencyBoundedRecovery();
			AssertFullRaster(input.candidate); // Reproduce the unresolved-owner route.
			// Establish the clean movie crop before the later overlay extends below it.
            auto admittedCrop = TrustedScopeCrop();
            admittedCrop.geometry = input.crop.geometry;
            admittedCrop.frameSourceSequence = input.crop.frameSourceSequence - 1;
            Assert::IsTrue(Evaluate(admittedCrop).owner == DecisionOwner::TRUSTED_CROP);
			auto admitted = AdmitCropPresentation({}, admittedCrop, Evaluate(admittedCrop), 9).state;
            Assert::IsTrue(admitted.available);
			for (uint64_t seq = 4013; seq < 4302; ++seq)
			{
				input.crop.frameSourceSequence = input.crop.currentVisibleSourceSequence = input.retentionSourceSequence = seq;
				const auto result = EvaluatePresentationRecovery(input);
				Assert::IsTrue(result.state.active);
				const auto final = AdmitCropPresentation(admitted, input.crop, result.presentation, 9);
				Assert::IsFalse(final.blocked);
				Assert::IsTrue(final.presentation.applyCrop);
				Assert::AreEqual(276, final.presentation.sourceBounds.top);
				Assert::AreEqual(2160, final.presentation.sourceBounds.bottom);
				Assert::AreEqual(3840.0 / 1884.0, final.presentation.sourceBounds.aspectRatio, .000001);
				Assert::AreEqual(1884, result.state.trustedCrop.bottom); // Movie geometry unchanged.
				input.previous = result.state;
			}
		}

        TEST_METHOD(BoundedRecoveryKeepsSafeEnvelopeWhenAcceptedCropChanges)
        {
            for (uint64_t first : {uint64_t{6534}, uint64_t{6789}})
            {
                auto input = AgencyBoundedRecovery(first);
                input.crop.currentVisibleBounds.top = input.observation.top = 54;
                input.crop.currentVisibleBounds.bottom = input.observation.bottom = 2106;
                auto initial = EvaluatePresentationRecovery(input);
                Assert::IsTrue(initial.boundedPresentation);
                Assert::AreEqual(54, initial.presentation.sourceBounds.top);
                Assert::AreEqual(2106, initial.presentation.sourceBounds.bottom);
                auto oldCrop = input.crop;
                oldCrop.presentationFailOpen = false;
                oldCrop.latestObservationSupportsCrop = true;
                auto admitted = AdmitCropPresentation({}, oldCrop, initial.presentation, 9).state;
                input.previous = initial.state;
                input.previous.samples = 3; // Old-contract votes must not be reused.
                auto& c = input.crop;
                c.geometry = {0,68,3840,2092,3840,2160,3840.0/2024.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                c.frameSourceSequence = input.retentionSourceSequence = first == 6534 ? 6542 : 6796;
                c.currentVisibleBoundsAvailable = false; // No content outside the NEW crop.
                c.presentationFailOpen = c.latestObservationIsProvisional = false;
                c.latestObservationSupportsCrop = true;
                c.latestObservationClassification = input.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                input.retentionBounds = input.observation = input.observedTrustedCrop = c.geometry;
                input.excludedBandsPixelSafe = true;
                input.candidate = Evaluate(c);
                Assert::IsTrue(input.candidate.applyCrop);
                auto recovered = EvaluatePresentationRecovery(input);
                Assert::IsTrue(recovered.boundedPresentation, L"Accepting a pixel-safe new crop must not throw away its safe wider fallback and expose full raster.");
                Assert::AreEqual(54, recovered.presentation.sourceBounds.top);
                Assert::AreEqual(2106, recovered.presentation.sourceBounds.bottom);
                Assert::IsTrue((recovered.gates & RECOVERY_CONTRACT) != 0);
                Assert::AreEqual(0u, recovered.samples);
                Assert::IsTrue(recovered.proofReset,L"Discarding old-contract votes must report the proof reset.");
                Assert::IsFalse(recovered.released);
                for (unsigned frame=0; frame<=recovered.required; ++frame)
                {
                    const auto final = AdmitCropPresentation(admitted, c, recovered.presentation, 9);
                    Assert::IsFalse(final.blocked);
                    Assert::IsTrue(final.presentation.applyCrop);
                    Assert::AreEqual(frame < recovered.required ? 54 : 68, final.presentation.sourceBounds.top);
                    Assert::AreEqual(frame < recovered.required ? 2106 : 2092, final.presentation.sourceBounds.bottom);
                    admitted = final.state;
                    input.previous = recovered.state;
                    ++c.frameSourceSequence; input.retentionSourceSequence = c.frameSourceSequence;
                    // A subtitle inside the larger picture no longer blocks final admission.
                    if (frame+1 == recovered.required)
                    {
                        c.latestObservationSupportsCrop = false;
                        c.latestObservationIsProvisional = true;
                        c.latestObservationClassification = input.observationClassification = ActivePictureClassification::PROVISIONAL;
                        c.frameLocalPresentationRetentionEvaluated = c.frameLocalPresentationRetentionSafe = true;
                        input.observation.top=276; input.observation.bottom=2016;
                    }
                    input.candidate = Evaluate(c);
                    recovered = EvaluatePresentationRecovery(input);
                }
            }
        }

        TEST_METHOD(BoundedContractChangeRejectsStaleUnsafeOrUntrustedPreservationEvidence)
        {
            for (int fault=0;fault<28;++fault)
            {
                auto input=ChangedContractBoundedRecovery();
                auto& c=input.crop;
                switch (fault)
                {
                case 0: input.measurementCurrent=false; break;
                case 1: input.retentionEvaluated=false; break;
                case 2: --input.retentionSourceSequence; break;
                case 3: --input.retentionSourceGeneration; break;
                case 4: input.retentionBounds.top+=4; break;
                case 5: input.excludedBandsPixelSafe=false; break;
                case 6: input.observationAvailable=false; break;
                case 7: input.observationClassification=c.latestObservationClassification=ActivePictureClassification::PROVISIONAL; break;
                case 8: c.latestObservationClassification=ActivePictureClassification::PROVISIONAL; break;
                case 9: input.observation.top=0; break;
                case 10: input.previous.fallbackBoundsAvailable=false; break;
                case 11: input.previous.fallbackBounds.top=55; break;
                case 12: input.previous.fallbackBounds.right=3842; break;
                case 13: input.previous.fallbackBounds.rasterWidth=3838; break;
                case 14: input.previous.fallbackBounds.top=100; break;
                case 15:
                    c.geometry.top=40; c.geometry.aspectRatio=3840.0/(2092-40);
                    input.retentionBounds=input.observation=input.observedTrustedCrop=c.geometry; break;
                case 16: input.globalNearBlack=true; break;
                case 17: input.nearBlackEvaluated=false; break;
                case 18: c.movingPictureTransition=true; break;
                case 19: c.nearBlackEpisodeFullRaster=true; break;
                case 20: c.classification=ActivePictureClassification::UNAVAILABLE; break;
                case 21: ++c.geometrySourceGeneration; break;
                case 22: input.observedTrustedCrop.top+=4; break;
                case 23: input.observedTrustedCrop.trustedBarAxes=ActivePictureBounds::BarAxes::NONE; break;
                case 24: c.latestObservationIsUnavailable=true; break;
                case 25: input.retentionBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE; break;
                case 26:
                    c.geometry.top=69; c.geometry.aspectRatio=3840.0/(2092-69);
                    input.retentionBounds=input.observation=input.observedTrustedCrop=c.geometry; break;
                case 27:
                    c.geometrySourceGeneration=c.frameSourceGeneration=input.retentionSourceGeneration=input.previous.sourceGeneration=0; break;
                }
                input.candidate=Evaluate(c);
                const auto result=EvaluatePresentationRecovery(input);
                const auto diagnostic=L"fault="+std::to_wstring(fault);
                Assert::IsFalse(result.boundedPresentation,diagnostic.c_str());
                Assert::IsFalse(result.released,diagnostic.c_str());
                AssertFullRaster(result.presentation);
            }
        }

        TEST_METHOD(BoundedContractChangeCannotInheritPartialProofOnRepeatedOrOlderSource)
        {
            for (int fault=0;fault<4;++fault)
            {
                auto input=ChangedContractBoundedRecovery();
                if (fault==0) input.cadenceRepeat=true;
                if (fault==1) input.crop.frameSourceSequence=input.previous.lastSourceSequence;
                if (fault==2) input.crop.frameSourceSequence=input.previous.lastSourceSequence-1;
                if (fault==3) input.crop.frameSourceSequence=0;
                input.retentionSourceSequence=input.crop.frameSourceSequence;
                input.candidate=Evaluate(input.crop);
                const auto result=EvaluatePresentationRecovery(input);
                Assert::IsTrue((result.gates & RECOVERY_CONTRACT)!=0);
                Assert::IsTrue((result.gates & RECOVERY_REPEAT)!=0);
                Assert::IsFalse(result.boundedPresentation);
                Assert::IsFalse(result.released);
                AssertFullRaster(result.presentation);
                Assert::AreEqual(0u,result.samples,
                    L"A changed crop cannot inherit old-contract proof votes even when its first observation is repeated or stale.");
                Assert::IsTrue(result.proofReset,L"Discarded partial proof must remain visible even on repeated/out-of-order frames.");
            }
        }

        TEST_METHOD(BoundedContractChangeDiscardsOldEnvelopeAcrossContextOrFullRasterAuthority)
        {
            for (int change=0;change<4;++change)
            {
                auto input=ChangedContractBoundedRecovery();
                if (change==0) ++input.presentationEpoch;
                if (change==1)
                {
                    ++input.crop.frameSourceGeneration;
                    input.crop.geometrySourceGeneration=input.retentionSourceGeneration=input.crop.frameSourceGeneration;
                }
                if (change==2)
                {
                    input.crop.rasterWidth=4096;
                    input.crop.geometry.rasterWidth=4096;
                    input.retentionBounds=input.observation=input.observedTrustedCrop=input.crop.geometry;
                }
                if (change==3) input.crop.fullRasterPresentationAuthoritative=true;
                input.candidate=Evaluate(input.crop);
                const auto result=EvaluatePresentationRecovery(input);
                Assert::IsTrue(result.ended);
                Assert::IsFalse(result.state.active);
                Assert::IsFalse(result.state.fallbackBoundsAvailable);
                Assert::IsFalse(result.boundedPresentation);
                Assert::AreEqual(input.candidate.sourceBounds.top,result.presentation.sourceBounds.top);
                if (change==3) AssertFullRaster(result.presentation);
                else Assert::IsTrue((result.gates & RECOVERY_CONTEXT)!=0);
            }
        }

        TEST_METHOD(BoundedContractPreservationDoesNotBypassFinalNewCropAdmission)
        {
            auto input=ChangedContractBoundedRecovery();
            auto old=TrustedScopeCrop();
            old.geometry=input.previous.trustedCrop;
            old.frameSourceSequence=6534;
            auto admitted=AdmitCropPresentation({},old,Evaluate(old),9).state;
            Assert::IsTrue(admitted.available);
            const auto preserved=EvaluatePresentationRecovery(input);
            Assert::IsTrue(preserved.boundedPresentation);
            // Presenting the safe old envelope still cannot acquire a different
            // crop contract when current picture acquisition is withdrawn.
            input.crop.latestObservationSupportsCrop=false;
            const auto final=AdmitCropPresentation(admitted,input.crop,preserved.presentation,9);
            Assert::IsTrue(final.blocked);
            AssertFullRaster(final.presentation);
            Assert::AreEqual(276,final.state.trustedCrop.top);
        }

		TEST_METHOD(BoundedRecoveryDoesNotPumpWithSmallChangesAndStillExposesMoreContent)
		{
			auto input = AgencyBoundedRecovery();
			input.crop.currentVisibleBounds.bottom = input.observation.bottom = 2040;
			for (int i = 0; i < 20; ++i)
			{
				input.crop.frameSourceSequence = input.crop.currentVisibleSourceSequence = input.retentionSourceSequence = 4013 + i;
				input.crop.currentVisibleBounds.bottom = input.observation.bottom = i % 2 ? 2036 : 2040;
				const auto result = EvaluatePresentationRecovery(input);
				Assert::IsTrue(result.presentation.applyCrop);
				Assert::AreEqual(2040, result.presentation.sourceBounds.bottom);
				input.previous = result.state;
			}
			++input.crop.frameSourceSequence; ++input.crop.currentVisibleSourceSequence; ++input.retentionSourceSequence;
			input.crop.currentVisibleBounds.top = input.observation.top = 0;
			input.crop.currentVisibleBounds.bottom = input.observation.bottom = 2160;
			AssertFullRaster(EvaluatePresentationRecovery(input).presentation);
		}

		TEST_METHOD(BoundedRecoveryRejectsMissingStaleConflictingOrUnacquiredEvidence)
		{
			for (int failure = 0; failure < 20; ++failure)
			{
				auto input = AgencyBoundedRecovery();
				switch (failure) {
				case 0: input.crop.currentVisibleBoundsAvailable = false; break;
				case 1: --input.crop.currentVisibleSourceSequence; break;
				case 2: --input.crop.currentVisibleSourceGeneration; break;
				case 3: input.measurementCurrent = false; break;
				case 4: input.retentionEvaluated = false; break;
				case 5: input.crop.currentVisibleBase.top += 4; break;
				case 6: input.retentionBounds.top += 4; break;
				case 7: input.globalNearBlack = true; break;
				case 8: input.nearBlackEvaluated = false; break;
				case 9: input.observation.top = 0; break;
				case 10: input.crop.currentVisibleBounds.top = 280; break;
				case 11: input.crop.currentVisibleBounds.bottom = 2162; break;
				case 12: input.crop.fullRasterPresentationAuthoritative = true; break;
				case 13: input.crop.latestObservationIsUnavailable = true; break;
				case 14: input.observationAvailable = false; break;
				case 15:
					input.crop.frameSourceGeneration = input.crop.geometrySourceGeneration =
						input.crop.currentVisibleSourceGeneration = input.retentionSourceGeneration = 0; break;
				case 16: input.crop.nearBlackEpisodeFullRaster = true; break;
				case 17: input.crop.movingPictureTransition = true; break;
				case 18: --input.retentionSourceGeneration; break;
				case 19: --input.retentionSourceSequence; break;
				}
				AssertFullRaster(EvaluatePresentationRecovery(input).presentation);
			}
			auto input = AgencyBoundedRecovery();
			const auto result = EvaluatePresentationRecovery(input);
			Assert::IsTrue(AdmitCropPresentation({}, input.crop, result.presentation, 9).blocked);
		}
		TEST_METHOD(BoundedRecoveryRealPixelsAgreeForNativeV210AndP010)
		{
			const int width = 384, height = 216;
			std::vector<uint16_t> planar(width * height * 3 / 2, uint16_t(512 << 6));
			std::vector<uint32_t> native(width / 6 * 4 * height);
			for (int y = 0; y < height; ++y)
			{
				const uint32_t level = y < 28 ? 64 : 281;
				for (int x = 0; x < width; ++x) planar[y * width + x] = uint16_t(level << 6);
				for (int x = 0; x < width; x += 6)
				{
					auto words = native.data() + y * width / 6 * 4 + x / 6 * 4;
					words[0] = 512 | (level << 10) | (512 << 20);
					words[1] = level | (512 << 10) | (level << 20);
					words[2] = 512 | (level << 10) | (512 << 20);
					words[3] = level | (512 << 10) | (level << 20);
				}
			}
			for (bool packed : {false, true})
			{
				AnalysisLumaSource source{reinterpret_cast<const uint8_t*>(planar.data()),
					planar.size() * sizeof(uint16_t), width, height, width * 2, width * 2,
					AnalysisLumaFormat::P010, VideoFrameEncoding::V210, ColorSpace::REC_709, 7};
				if (packed) {
					source.data = reinterpret_cast<const uint8_t*>(native.data());
					source.dataBytes = native.size() * sizeof(uint32_t);
					source.rowBytes = width / 6 * 16; source.chromaRowBytes = 0;
					source.format = AnalysisLumaFormat::NativeYuv422;
				}
				auto input = AgencyBoundedRecovery();
				auto& crop = input.crop;
				crop.rasterWidth = width; crop.rasterHeight = height;
				crop.geometry = {0,28,width,188,width,height,384.0/160.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
				const auto pixels = EvaluateActivePicturePresentationRetention(source, crop.geometry);
				Assert::IsTrue(pixels.analysisValid && pixels.presentationValid && pixels.outwardVisibleBoundsAvailable);
				Assert::IsFalse(pixels.excludedBandsPixelSafe);
				Assert::AreEqual(64.0, pixels.excludedTop.lumaP90, .001);
				input.retentionBounds = crop.currentVisibleBase = crop.geometry;
				crop.currentVisibleBounds = pixels.outwardVisibleBounds;
				input.observation = pixels.activePicture.proposedBounds;
				input.observationClassification = crop.latestObservationClassification = pixels.activePicture.classification;
				// Include the renderer's coarse observation, as production does.
				const auto coarse = ResolvePresentationObservation(crop.geometry, pixels.activePicture, pixels);
				crop.currentVisibleBounds.top = std::min(crop.currentVisibleBounds.top, coarse.bounds.top);
				crop.currentVisibleBounds.bottom = std::max(crop.currentVisibleBounds.bottom, coarse.bounds.bottom);
				input.candidate = Evaluate(crop);
				const auto result = EvaluatePresentationRecovery(input);
				Assert::IsTrue(result.boundedPresentation);
				Assert::AreEqual(28, result.presentation.sourceBounds.top);
				Assert::AreEqual(height, result.presentation.sourceBounds.bottom);
			}
		}

		TEST_METHOD(BoundedRecoveryReturnsInwardOnlyAfterExistingProofAndClearsOnContextChange)
		{
			auto input = AgencyBoundedRecovery();
			input.previous = EvaluatePresentationRecovery(input).state;
			for (unsigned i = 1; i <= EvaluatePresentationRecovery(AgencyBoundedRecovery()).required; ++i)
			{
				++input.crop.frameSourceSequence;
				input.retentionSourceSequence = input.crop.frameSourceSequence;
				input.crop.currentVisibleBoundsAvailable = false;
				input.excludedBandsPixelSafe = true;
				input.crop.presentationFailOpen = input.crop.latestObservationIsProvisional = false;
				input.crop.latestObservationSupportsCrop = true;
				input.crop.latestObservationClassification = input.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
				input.observation = input.observedTrustedCrop = input.crop.geometry;
				input.candidate = Evaluate(input.crop);
				const auto result = EvaluatePresentationRecovery(input);
				Assert::AreEqual(i < result.required ? 2160 : 1884, result.presentation.sourceBounds.bottom);
				Assert::AreEqual(i == result.required, result.released);
				input.previous = result.state;
			}
			auto stale = AgencyBoundedRecovery();
			stale.previous = EvaluatePresentationRecovery(stale).state;
			++stale.presentationEpoch;
			const auto reset = EvaluatePresentationRecovery(stale);
			Assert::IsFalse(reset.state.active || reset.state.fallbackBoundsAvailable);
			AssertFullRaster(reset.presentation);
		}

		TEST_METHOD(BoundedRecoveryHandlesEitherAxisAndRoundsOnlyOutward)
		{
			for (int edge = 0; edge < 4; ++edge)
			{
				auto input = AgencyBoundedRecovery();
				input.crop.geometry = {200,276,3640,1884,3840,2160,3440.0/1608.0,ActivePictureBounds::BarAxes::BOTH};
				input.retentionBounds = input.crop.currentVisibleBase = input.crop.geometry;
				auto visible = input.crop.geometry;
				if (edge == 0) visible.left = 101;
				if (edge == 1) visible.top = 101;
				if (edge == 2) visible.right = 3739;
				if (edge == 3) visible.bottom = 2039;
				input.crop.currentVisibleBounds = input.observation = visible;
				input.candidate = Evaluate(input.crop);
				const auto result = EvaluatePresentationRecovery(input);
				Assert::IsTrue(result.boundedPresentation);
				Assert::AreEqual(edge == 0 ? 100 : 200, result.presentation.sourceBounds.left);
				Assert::AreEqual(edge == 1 ? 100 : 276, result.presentation.sourceBounds.top);
				Assert::AreEqual(edge == 2 ? 3740 : 3640, result.presentation.sourceBounds.right);
				Assert::AreEqual(edge == 3 ? 2040 : 1884, result.presentation.sourceBounds.bottom);
			}
		}
		TEST_METHOD(CurrentCertifiedFitCanResolveAnAlreadyArmedRecovery)
		{
			Input crop = TrustedScopeCrop();
			crop.geometry = {192,440,3648,1720,3840,2160,2.7,ActivePictureBounds::BarAxes::BOTH};
			crop.frameSourceSequence = 2424;
			crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.barCropRefinementHorizontalConflict = true;
			crop.outwardPresentationActive = crop.outwardExpansionAvailable = true;
			crop.outwardExpansionSourceGeneration = crop.currentVisibleSourceGeneration = 7;
			crop.currentVisibleSourceSequence = crop.frameSourceSequence;
			crop.currentVisibleBase = crop.geometry;
			crop.currentVisibleBoundsAvailable = true;
			crop.currentVisibleBounds = crop.geometry; crop.currentVisibleBounds.right = 3698;
			crop.outwardExpansion = crop.currentVisibleBounds; crop.outwardExpansion.right = 3722;
			PresentationRecoveryInput input;
			input.crop = crop; input.candidate = Evaluate(crop);
			input.previous.active = true; input.previous.sourceGeneration = 7;
			input.previous.trustedCrop = input.retentionBounds = crop.geometry;
			input.measurementCurrent = input.retentionEvaluated = input.nearBlackEvaluated = true;
			input.retentionSourceGeneration = 7; input.retentionSourceSequence = crop.frameSourceSequence;
			input.excludedBandsPixelSafe = false;
			const auto d = EvaluatePresentationRecovery(input);
			Assert::IsTrue(d.released && d.ended && d.presentation.applyCrop);
			Assert::AreEqual(3722, d.presentation.sourceBounds.right);
			for (int failure=0; failure<8; ++failure)
			{
				auto bad = input;
				switch (failure) {
				case 0: bad.measurementCurrent=false; break;
				case 1: --bad.crop.currentVisibleSourceSequence; break;
				case 2: --bad.retentionSourceGeneration; break;
				case 3: bad.globalNearBlack=true; break;
				case 4: bad.crop.currentVisibleBounds.right=3730; break;
				case 5: bad.retentionBounds.top+=4; break;
				case 6: bad.cadenceRepeat=true; break;
				case 7:
					bad.crop.verticalTranslationEngageBaseRetentionActive=true;
					bad.crop.verticalTranslationBase=bad.crop.geometry;
					bad.crop.verticalTranslationSourceGeneration=7;
					bad.crop.currentVisibleBounds.bottom=2160;
					break;
				}
				AssertFullRaster(EvaluatePresentationRecovery(bad).presentation);
			}
		}


		TEST_METHOD(LoggedLookaheadConflictKeepsCropFillAndSubtitleScaleStable)
		{
			// 0dea replay: live2242/2243 reject this small change, but queue2244
			// formerly replaced the anchor, crossed2.41, and changed final scale.
			const ActivePictureBounds anchor = {176,356,3664,1804,3840,2160,3488.0/1448.0,ActivePictureBounds::BarAxes::BOTH};
			const ActivePictureBounds candidate = {192,372,3648,1788,3840,2160,3456.0/1416.0,ActivePictureBounds::BarAxes::BOTH};
			auto queueBase = anchor; queueBase.top = 0; queueBase.bottom = 2160;
			queueBase.aspectRatio = 3488.0/2160.0; queueBase.trustedBarAxes = ActivePictureBounds::BarAxes::LEFT_RIGHT;
			ActivePictureTransitionDecision publication;
			// Reconstruct the stale queue publication recorded by the old build.
			// Current preview models now veto this all-sided inset before queueing,
			// while live adoption still independently rejects its stale reference.
			publication.publish = publication.stable = true;
			publication.bounds = candidate;
			publication.stableBounds = queueBase;
			for (bool lookahead : {false,true})
			{
				ActivePictureTransitionModel live;
				for (uint64_t f = 1; f <= 4; ++f)
					live.Observe({anchor,f,true,ActivePictureClassification::BAR_CROP_TRUSTED,24.0});
				PresentationRecoveryState recovery;
				for (uint64_t seq = 2242; seq <= 2484; ++seq)
				{
					if (lookahead && seq == 2244)
					{
						ActivePictureFrameDecision scheduled;
						scheduled.transition = publication;
						scheduled.observationIdentity = scheduled.effectiveIdentity = {7,seq,seq,seq,1,1,1};
						Assert::AreEqual(static_cast<int>(ActivePictureScheduledDecisionValidation::ACCEPTED),
							static_cast<int>(ValidateActivePictureScheduledDecision(scheduled,scheduled.effectiveIdentity,
								candidate,ActivePictureClassification::BAR_CROP_TRUSTED)));
						Assert::IsFalse(live.AdoptPublishedDecision(publication,ActivePictureClassification::BAR_CROP_TRUSTED));
					}
					const auto geometry = live.Observe({candidate,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24.0});
					Assert::IsFalse(geometry.publish);
					Input crop = TrustedScopeCrop();
					crop.geometry = geometry.bounds; crop.frameSourceSequence = seq;
					// Include onset, sustained burned-in subtitle visibility, and release.
					crop.verticalTranslationActive = seq >= 2260 && seq < 2400;
					crop.verticalTranslationPixels = crop.verticalTranslationActive ? 84 : 0;
					crop.verticalTranslationBase = anchor;
					crop.verticalTranslationSourceGeneration = 7;
					PresentationRecoveryInput input;
					input.previous = recovery; input.crop = crop; input.candidate = Evaluate(crop);
					input.measurementCurrent = input.retentionEvaluated = input.nearBlackEvaluated = true;
					input.excludedBandsPixelSafe = !crop.verticalTranslationActive;
					input.retentionBounds = anchor; input.retentionSourceGeneration = 7;
					input.retentionSourceSequence = seq; input.framesPerSecond = 24.0;
					const auto presented = EvaluatePresentationRecovery(input);
					recovery = presented.state;
					Assert::IsTrue(presented.presentation.applyCrop);
					Assert::IsFalse(recovery.active);
					AspectLimitFillInput fill;
					fill.sourceBounds = presented.presentation.sourceBounds;
					fill.trustedContentAuthorityAccepted = true;
					fill.cropWiderContentToFillScreen = fill.widerLimitConfigured = true;
					fill.widerAspectLimit = 2.41; fill.screenAspect = 2.35;
					const auto final = EvaluateAspectLimitFill(fill);
					Assert::IsTrue(final.applied);
					Assert::AreEqual(3402,final.sourceBounds.right-final.sourceBounds.left);
					Assert::AreEqual(1448,final.sourceBounds.bottom-final.sourceBounds.top);
					Assert::AreEqual(356+crop.verticalTranslationPixels,final.sourceBounds.top);
				}
			}
		}

		TEST_METHOD(RetainedAspectDoesNotSuppressCurrentOutsidePicture)
		{
			Input crop = TrustedScopeCrop();
			crop.geometry = {176,356,3664,1804,3840,2160,3488.0/1448.0,ActivePictureBounds::BarAxes::BOTH};
			crop.frameSourceSequence = 2273;
			crop.barCropRefinementHorizontalConflict = true;
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.outwardPresentationActive = crop.outwardExpansionAvailable = true;
			crop.outwardExpansionSourceGeneration = 7;
			crop.outwardExpansion = crop.geometry;
			crop.outwardExpansion.left = 128; crop.outwardExpansion.right = 3712;
			crop.currentVisibleBoundsAvailable = true;
			crop.currentVisibleBounds = crop.outwardExpansion;
			crop.currentVisibleBase = crop.geometry;
			crop.currentVisibleSourceGeneration = 7;
			crop.currentVisibleSourceSequence = crop.frameSourceSequence;
			crop.frameLocalPresentationRetentionEvaluated = true;
			const auto visible = Evaluate(crop);
			Assert::IsTrue(visible.applyCrop);
			Assert::IsTrue(visible.horizontalExpansionPixelBounded);
			Assert::IsTrue(visible.sourceBounds.left <= 128 && visible.sourceBounds.right >= 3712);
			AspectLimitFillInput fill;
			fill.sourceBounds = visible.sourceBounds;
			fill.trustedContentAuthorityAccepted = true;
			fill.cropWiderContentToFillScreen = fill.widerLimitConfigured = true;
			fill.widerAspectLimit = 2.41; fill.screenAspect = 2.35;
			const auto final = EvaluateAspectLimitFill(fill);
			Assert::IsFalse(final.applied);
			Assert::IsTrue(final.sourceBounds.left <= 128 && final.sourceBounds.right >= 3712);
		}


		// Composes the production admission, model, dense FIT, envelope,
		// inspection, recovery and final presentation admission paths. Source
		// coordinates/evidence are the 22:08:15 log, not a decoded pixel replay.
		static void ReplayBroadPictureTransition(bool lookahead, int densePeriod, int edgeNoise=0, bool bothEdges=false, bool staleQueuedTarget=false)
		{
			const ActivePictureBounds scope{0,276,3840,1884,3840,2160,3840.0/1608,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			const ActivePictureBounds taller{0,68,3840,2092,3840,2160,3840.0/2024,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			// Stable source pixels; only detector measurements jitter. Recheck
			// the newly committed rectangle against these same P010 bytes,
			// exactly as the renderer does after a model publication.
			std::vector<uint16_t> pixels(3840*2160*3/2,uint16_t(512<<6));
			for (int y=0;y<2160;++y)
				if (y<taller.top || y>=taller.bottom)
					std::fill(pixels.begin()+size_t(y)*3840,pixels.begin()+size_t(y+1)*3840,uint16_t(64<<6));
			AnalysisLumaSource source;
			source.data=reinterpret_cast<const uint8_t*>(pixels.data()); source.dataBytes=pixels.size()*sizeof(uint16_t);
			source.width=3840; source.height=2160; source.rowBytes=source.chromaRowBytes=3840*sizeof(uint16_t);
			source.format=AnalysisLumaFormat::P010;
			ActivePictureTransitionModel model;
			for (uint64_t seq=1;seq<=4;++seq)
				model.Observe({scope,seq,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
			Input crop=TrustedScopeCrop(); crop.geometry=scope;
			crop.frameSourceSequence=3728;
			CropPresentationAdmissionState admitted=AdmitCropPresentation({},crop,Evaluate(crop),9).state;
			PresentationRecoveryState recovery;
			VerticalInspectionBridgeState inspection;
			OutwardPictureConfirmationState outward;
			VerticalFitConfirmationState dense;
			VerticalBarContentDecision accepted;
			ActivePictureBounds geometry=scope, last=scope;
			for (uint64_t cycle : {0ULL,20ULL})
			{
			int changes=0;
			for (uint64_t seq=3729+cycle;seq<=3735+cycle;++seq)
			{
				auto observed=taller;
				if ((seq&1)!=0) { observed.top+=edgeNoise; if (bothEdges) observed.bottom-=edgeNoise; }
				observed.aspectRatio=double(observed.right-observed.left)/(observed.bottom-observed.top);
				const auto measuredBase=geometry;
				TransitionAdmissionInput input;
				input.trustedGeometry=input.presentationBeforeObservation=geometry;
				input.trustedGeometryAvailable=input.compatiblePresentation=input.evidence.available=true;
				input.trustedGeneration=input.sourceGeneration=7; input.sourceSequence=seq;
				input.framesPerSecond=24;
				input.evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
				input.evidence.trustedBounds=input.evidence.proposedBounds=input.outwardCandidate=observed;
				input.retention=EvaluateActivePicturePresentationRetention(source,geometry);
				input.retention.expansionStripsAvailable=true;
				input.retention.expansionBase=geometry; input.retention.expansionCandidate=observed;
				auto& edge=input.retention.expandingTop;
				edge.barPixels=206; edge.blackFraction=.2; edge.continuity=.4; edge.lumaP90=300;
				input.retention.expandingBottom=edge;
				input.previousOutward=outward;
				const bool eligible=model.WouldAdmitGeometryChange(MakeActivePictureObservation(input.evidence,seq,24));
				const auto admission=EvaluateTransitionAdmission(input);
				outward=admission.outward.state;
				ActivePictureTransitionDecision transition;
				bool adopted=false;
				if (lookahead && seq==3731+cycle)
				{
					ActivePictureFrameDecision scheduled;
					scheduled.transition.publish=scheduled.transition.stable=true;
					scheduled.transition.bounds=observed; scheduled.transition.stableBounds=scope;
					if (staleQueuedTarget) scheduled.transition.bounds.top+=4;
					scheduled.transition.stableBounds.top+=4; // logged queue/live scan-step disagreement
					scheduled.effectiveIdentity.acceptedSequence=scheduled.observationIdentity.acceptedSequence=seq;
					scheduled.effectiveIdentity.transportGeneration=scheduled.observationIdentity.transportGeneration=7;
					const auto validation=ValidateActivePictureScheduledDecision(scheduled,scheduled.effectiveIdentity,observed,input.evidence.classification);
					adopted=validation==ActivePictureScheduledDecisionValidation::ACCEPTED &&
						model.AdoptPublishedDecision(scheduled.transition,input.evidence.classification,admission.observation.transitionDeferred,
							nullptr,&admission.observation.axisEvidence,admission.outward.authoritative);
					if (adopted) transition=scheduled.transition;
				}
				if (!adopted) transition=model.Observe(admission.observation);
				const auto handoff=MakePictureTransitionHandoff(input,admission,transition,eligible,9);
				if (transition.publish) { geometry=transition.bounds; dense={}; accepted={}; }
				const auto currentPixels=ResolveActivePictureRetentionHandoff(source,measuredBase,input.retention,geometry);
				const bool sameBase=geometry.top==scope.top;
				const bool denseCurrent=sameBase && ((seq-3729-cycle)%densePeriod==0);
				if (denseCurrent)
				{
					VerticalBarContentInput bar;
					bar.upperContent=bar.lowerContent=true;
					bar.upperOccupiedDepth=bar.lowerOccupiedDepth=206;
					bar.upperPeakSamples=1800; bar.lowerPeakSamples=1653;
					bar.upperBarPixels=bar.lowerBarPixels=276; bar.sampledColumns=1800;
					bar.upperRequiredShift=bar.lowerRequiredShift=208;
					const auto fit=ConfirmVerticalFit(dense,EvaluateVerticalBarContent(bar),seq);
					dense=fit.state; accepted=fit.effective;
				}
				VerticalBarPresentationResolutionInput resolution;
				resolution.detailedAction=accepted.action;
				resolution.denseVerticalArbitrationEnabled=true;
				resolution.genericUpperExpansion=resolution.genericLowerExpansion=sameBase;
				resolution.genericVerticalFitConfirmed=sameBase;
				resolution.genericVerticalFitAuthoritative=!sameBase;
				resolution.genericUpperBound=54; resolution.genericLowerBound=2106;
				resolution.authoritativeTop=geometry.top; resolution.authoritativeBottom=geometry.bottom;
				resolution.rasterHeight=2160;
				const auto routing=ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(resolution));
				crop=TrustedScopeCrop(); crop.geometry=geometry; crop.frameSourceSequence=seq;
				crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
				const bool containsObserved=geometry.left<=observed.left && geometry.top<=observed.top &&
					geometry.right>=observed.right && geometry.bottom>=observed.bottom;
				const bool sameMeasuredBase=geometry.top==currentPixels.bounds.top && geometry.bottom==currentPixels.bounds.bottom;
				crop.latestObservationSupportsCrop=containsObserved || (sameMeasuredBase &&
					IsPixelSafeCropReaffirmation(geometry,observed,currentPixels.evidence.excludedBandsPixelSafe));
				crop.barCropRefinementPending=!crop.latestObservationSupportsCrop;
				crop.frameLocalPresentationRetentionEvaluated=true; crop.frameLocalPresentationRetentionSafe=currentPixels.evidence.excludedBandsPixelSafe;
				crop.verticalTranslationBase=geometry; crop.verticalTranslationSourceGeneration=7;
				crop.verticalFitConfirmationPending=dense.confirmations>0 && dense.confirmations<2;
				crop.outwardPresentationActive=routing.fitActive;
				if (routing.fitActive)
				{
					PresentationEnvelopeGeometryInput envelope;
					envelope.trustedPicture=geometry; envelope.observedContent=observed;
					envelope.observedContentAvailable=envelope.expandTop=envelope.expandBottom=true;
					envelope.verticalPadding=48;
					const auto expansion=BuildPresentationEnvelope(envelope);
					crop.outwardExpansion=expansion.bounds; crop.outwardExpansionAvailable=expansion.expanded;
					crop.outwardExpansionSourceGeneration=7;
				}
				crop.pictureTransitionHandoff=handoff; crop.framePresentationEpoch=9;
				auto candidate=Evaluate(crop);
				VerticalInspectionBridgeInput bridge;
				bridge.previous=inspection; bridge.candidate=sameBase;
				bridge.retentionRequested=sameBase && !candidate.applyCrop && candidate.withdrawalCause==WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED;
				bridge.denseAnalysisCompleted=denseCurrent;
				VerticalInspectionFitResolutionInput fitResolution;
				fitResolution.confirmedDenseFit=routing.fitActive; fitResolution.denseAnalysisCurrent=denseCurrent;
				fitResolution.outwardExpansionAvailable=crop.outwardExpansionAvailable;
				fitResolution.trustedBase=geometry; fitResolution.outwardExpansion=crop.outwardExpansion;
				fitResolution.outwardExpansionSourceGeneration=fitResolution.frameSourceGeneration=7;
				bridge.confirmedVerticalFitResolved=CanResolveVerticalInspectionWithConfirmedFit(fitResolution);
				bridge.verticalPresentationOwnerAvailable=HasCurrentPictureTransitionHandoff(crop) || crop.verticalFitConfirmationPending || routing.fitActive;
				bridge.cropAuthorityResolved=crop.latestObservationSupportsCrop;
				bridge.sourceGeneration=7; bridge.sourceSequence=seq; bridge.presentationEpoch=9;
				bridge.trustedBase=geometry;
				const auto inspected=UpdateVerticalInspectionBridge(bridge); inspection=inspected.state;
				if (inspected.retain) { crop.verticalInspectionPending=true; crop.verticalInspectionSourceGeneration=7;
					crop.verticalInspectionSourceSequence=seq; candidate=Evaluate(crop); }
				if (inspection.failOpenLatched) { crop.presentationFailOpen=true; candidate=Evaluate(crop); }
				PresentationRecoveryInput recover;
				recover.previous=recovery; recover.crop=crop; recover.candidate=candidate;
				recover.measurementCurrent=recover.retentionEvaluated=recover.nearBlackEvaluated=true;
				recover.retentionBounds=currentPixels.bounds; recover.retentionSourceGeneration=7; recover.retentionSourceSequence=seq;
				recover.observationAvailable=currentPixels.evidence.proposedBoundsAvailable;
				recover.observation=currentPixels.evidence.activePicture.proposedBounds;
				recover.observedTrustedCrop=currentPixels.evidence.activePicture.trustedBounds;
				recover.observationClassification=currentPixels.evidence.activePicture.classification;
				recover.excludedBandsPixelSafe=currentPixels.evidence.excludedBandsPixelSafe; recover.presentationEpoch=9;
				recover.currentTick=seq*42; recover.framesPerSecond=24;
				auto recovered=EvaluatePresentationRecovery(recover); recovery=recovered.state;
				const auto visible=AdmitCropPresentation(admitted,crop,recovered.presentation,9);
				admitted=visible.state;
				const auto diagnostic=L"sequence="+std::to_wstring(seq)+L" noise="+std::to_wstring(edgeNoise)+
					L" lookahead="+std::to_wstring(lookahead)+L" published="+std::to_wstring(transition.publish)+
					L" logical_top="+std::to_wstring(geometry.top)+L" observed_top="+std::to_wstring(observed.top)+
					L" shown_top="+std::to_wstring(visible.presentation.sourceBounds.top)+
					L" pixel_safe="+std::to_wstring(currentPixels.evidence.excludedBandsPixelSafe);
				Assert::IsTrue(visible.presentation.applyCrop,diagnostic.c_str());
				const auto& shown=visible.presentation.sourceBounds;
				if (shown.top!=last.top || shown.bottom!=last.bottom) ++changes;
				last=shown;
				// No padded 1.811 presentation between 2.388 and 1.897.
				Assert::IsTrue((shown.top==scope.top && shown.bottom==scope.bottom) ||
					(shown.top==geometry.top && shown.bottom==geometry.bottom),diagnostic.c_str());
				Assert::AreEqual(transition.publish || !sameBase ? geometry.top : scope.top,shown.top,diagnostic.c_str());
			}
			Assert::AreEqual(1,changes);
			Assert::IsTrue(std::abs(last.top-taller.top)<=4);
			// The return to scope still uses normal inward confirmation.
			// This handoff grants no inward look-ahead/reference tolerance.
			int inwardChanges=0;
			for (uint64_t seq=3736+cycle;seq<=3739+cycle;++seq)
			{
				TransitionAdmissionInput input;
				input.trustedGeometry=input.presentationBeforeObservation=geometry;
				input.trustedGeometryAvailable=input.compatiblePresentation=input.evidence.available=true;
				input.trustedGeneration=input.sourceGeneration=7; input.sourceSequence=seq;
				input.framesPerSecond=24;
				input.evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
				input.evidence.trustedBounds=input.evidence.proposedBounds=input.outwardCandidate=scope;
				input.retention.analysisValid=input.retention.presentationValid=true;
				input.retention.excludedBandsPixelSafe=true;
				const bool eligible=model.WouldAdmitGeometryChange(MakeActivePictureObservation(input.evidence,seq,24));
				const auto admission=EvaluateTransitionAdmission(input);
				const auto transition=model.Observe(admission.observation);
				Assert::IsFalse(MakePictureTransitionHandoff(input,admission,transition,eligible,9).active);
				if (transition.publish) geometry=transition.bounds;
				crop=TrustedScopeCrop(); crop.geometry=geometry; crop.frameSourceSequence=seq;
				crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
				const auto visible=AdmitCropPresentation(admitted,crop,Evaluate(crop),9);
				admitted=visible.state;
				Assert::IsTrue(visible.presentation.applyCrop);
				if (visible.presentation.sourceBounds.top!=last.top) ++inwardChanges;
				last=visible.presentation.sourceBounds;
			}
			Assert::AreEqual(1,inwardChanges);
			Assert::AreEqual(scope.top,last.top);
			}
		}

		TEST_METHOD(LoggedPictureTransitionWithoutLookaheadHasOnePresentationChange)
		{
			for (int period : {2,1,3}) ReplayBroadPictureTransition(false,period);
		}

		TEST_METHOD(LoggedPictureTransitionWithLookaheadHasOnePresentationChange)
		{
			for (int period : {2,1,3}) ReplayBroadPictureTransition(true,period);
		}

		TEST_METHOD(SamplingNoiseAcrossWholeTransitionNeverWithdrawsOrResizesAgain)
		{
			for (bool lookahead : {false,true})
				for (int noise : {-4,4})
					for (bool bothEdges : {false,true})
						ReplayBroadPictureTransition(lookahead,2,noise,bothEdges);
		}

		TEST_METHOD(StaleQueuedTargetFallsBackWithoutPresentationBounce)
		{
			for (int noise : {-4,4})
				ReplayBroadPictureTransition(true,2,noise,true,true);
		}

		TEST_METHOD(QueuedPublicationCannotOverrideCurrentAdmissionVeto)
		{
			ActivePictureTransitionModel model;
			ActivePictureTransitionDecision queued;
			queued.publish = queued.stable = true;
			queued.bounds = TrustedScopeCrop().geometry;
			Assert::IsFalse(model.AdoptPublishedDecision(queued, ActivePictureClassification::BAR_CROP_TRUSTED, true));
			Assert::IsFalse(model.Observe({}).stable);
			Assert::IsTrue(model.AdoptPublishedDecision(queued, ActivePictureClassification::BAR_CROP_TRUSTED, false));
		}

        TEST_METHOD(CropDiagnosticsStayQuietWhenInactiveAndHeartbeatUnresolvedState)
        {
            CropDiagnosticThrottle throttle;
            for (uint64_t now=0;now<=10000;now+=100)
                Assert::IsFalse(throttle.Observe(now,7,9,now+1,false,now));
            Assert::IsFalse(throttle.logged);
            Assert::IsTrue(throttle.Observe(10100,7,9,10102,true,44));
            Assert::AreEqual(uint64_t{0},throttle.emittedSuppressed);
            Assert::IsFalse(throttle.Observe(12099,7,9,10103,true,44));
            Assert::IsTrue(throttle.Observe(12100,7,9,10104,true,44));
            Assert::AreEqual(uint64_t{0},throttle.emittedSuppressed);
            Assert::IsFalse(throttle.Observe(14099,7,9,10105,true,44));
            Assert::IsTrue(throttle.Observe(14100,7,9,10106,true,44));
        }

        TEST_METHOD(CropDiagnosticsCapOscillatingStartEndAndCoalesceSuppressedEvents)
        {
            CropDiagnosticThrottle throttle;
            Assert::IsTrue(throttle.Observe(1000,7,9,100,true,1));
            Assert::IsFalse(throttle.Observe(1100,7,9,101,false,0));
            Assert::IsFalse(throttle.Observe(1200,7,9,102,true,1));
            Assert::IsFalse(throttle.Observe(1300,7,9,103,true,2));
            Assert::IsFalse(throttle.Observe(1499,7,9,104,false,0));
            Assert::IsTrue(throttle.Observe(1500,7,9,105,false,0));
            Assert::AreEqual(uint64_t{4},throttle.emittedSuppressed);
            Assert::AreEqual(uint64_t{0},throttle.suppressed);
            // The coalesced end is final: idle state does not get heartbeats.
            Assert::IsFalse(throttle.Observe(3500,7,9,106,false,0));
            Assert::IsFalse(throttle.Observe(9500,7,9,107,false,0));
        }

        TEST_METHOD(CropDiagnosticPendingEndFlushesAtCapWithoutAnotherStateChange)
        {
            CropDiagnosticThrottle throttle;
            Assert::IsTrue(throttle.Observe(1000,7,9,1,true,11));
            Assert::IsFalse(throttle.Observe(1001,7,9,2,false,11));
            Assert::IsTrue(throttle.pending);
            Assert::IsFalse(throttle.Observe(1499,7,9,3,false,11));
            Assert::IsTrue(throttle.Observe(1500,7,9,4,false,11));
            Assert::AreEqual(uint64_t{1},throttle.emittedSuppressed);
            Assert::IsFalse(throttle.pending);
            Assert::IsFalse(throttle.wasActive);
        }

        TEST_METHOD(CropDiagnosticSameSourceFrameCannotSpamEventBundles)
        {
            CropDiagnosticThrottle throttle;
            uint64_t emitted=0,lastEmission=0;
            for (uint64_t now=1000;now<=3000;++now)
            {
                const bool emit=throttle.Observe(now,7,9,100,true,1,true);
                if (emit)
                {
                    if (emitted!=0)
                    {
                        Assert::IsTrue(now-lastEmission>=500);
                        Assert::AreEqual(uint64_t{499},throttle.emittedSuppressed);
                    }
                    lastEmission=now;
                    ++emitted;
                }
            }
            Assert::AreEqual(uint64_t{5},emitted);
            Assert::AreEqual(uint64_t{100},throttle.lastSequence);
        }

        TEST_METHOD(CropDiagnosticContextAndClockRollbackDiscardPreviousThrottleState)
        {
            for (int context=0;context<4;++context)
            {
                CropDiagnosticThrottle throttle;
                Assert::IsTrue(throttle.Observe(1000,7,9,100,true,1));
                Assert::IsFalse(throttle.Observe(1100,7,9,101,true,2));
                Assert::AreEqual(uint64_t{1},throttle.suppressed);
                uint64_t now=1101,generation=7,epoch=9,sequence=102;
                if (context==0) ++generation;
                if (context==1) ++epoch;
                if (context==2) sequence=99;
                if (context==3) now=999;
                Assert::IsTrue(throttle.Observe(now,generation,epoch,sequence,true,3));
                Assert::AreEqual(uint64_t{0},throttle.emittedSuppressed);
                Assert::AreEqual(uint64_t{0},throttle.suppressed);
                Assert::AreEqual(generation,throttle.generation);
                Assert::AreEqual(epoch,throttle.epoch);
                Assert::AreEqual(sequence,throttle.lastSequence);
                Assert::IsFalse(throttle.Observe(now+499,generation,epoch,sequence,true,3));
            }
            CropDiagnosticThrottle throttle;
            Assert::IsTrue(throttle.Observe(1000,7,9,100,true,1));
            Assert::IsFalse(throttle.Observe(1100,7,9,101,false,0));
            Assert::IsFalse(throttle.Observe(1101,8,9,1,false,0));
            Assert::IsFalse(throttle.pending);
            Assert::IsFalse(throttle.logged);
        }

        TEST_METHOD(OutwardDiagnosticReasonsDescribeEvidenceGatesWithoutAddingAuthority)
        {
            Assert::AreEqual("not-evaluated",OutwardPictureConfirmationDecision{}.diagnosticReason);
            const ActivePictureBounds base={200,276,3640,1884,3840,2160,3440.0/1608.0,ActivePictureBounds::BarAxes::BOTH};
            const ActivePictureBounds target={0,68,3840,2092,3840,2160,3840.0/2024.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            for (int gate=0;gate<7;++gate)
            {
                auto candidate=target;
                ActivePicturePresentationRetentionEvidence evidence;
                evidence.analysisValid=evidence.presentationValid=evidence.expansionStripsAvailable=true;
                evidence.expansionBase=base; evidence.expansionCandidate=candidate;
                evidence.expandingTop.barPixels=208;
                evidence.expandingTop.blackFraction=.3;
                evidence.expandingTop.continuity=.4;
                evidence.expandingTop.lumaP90=300;
                evidence.expandingBottom=evidence.expandingLeft=evidence.expandingRight=evidence.expandingTop;
                const char* reason=nullptr;
                switch (gate)
                {
                case 0: candidate=base; reason="no-compatible-expansion"; break;
                case 1:
                    candidate=base; candidate.top-=2; candidate.bottom+=2;
                    reason="measurement-step-only"; break;
                case 2: evidence.expansionBase.top+=4; reason="strip-certificate-mismatch"; break;
                case 3: evidence.analysisValid=false; reason="measurement-invalid"; break;
                case 4: evidence.expandingTop.blackFraction=.99; reason="vertical-not-broad"; break;
                case 5: evidence.expandingLeft.blackFraction=.99; reason="horizontal-not-broad"; break;
                case 6:
                    evidence.expandingTop.blackFraction=evidence.expandingLeft.blackFraction=.99;
                    reason="both-axes-not-broad"; break;
                }
                const auto decision=ConfirmOutwardPictureTransition({},base,candidate,evidence,7,100);
                Assert::AreEqual(reason,decision.diagnosticReason);
                Assert::IsFalse(decision.authoritative);
                Assert::IsFalse(decision.broadOpposingPicture);
                Assert::AreEqual(0u,decision.state.confirmations);
                Assert::AreEqual(gate>=2,decision.outwardTransition);
            }
        }

		TEST_METHOD(OutwardProofResetsForInvalidEvidenceGapsAndGenerationChanges)
		{
			const auto scope = TrustedScopeCrop().geometry;
			auto larger = scope; larger.top = 100;
			ActivePicturePresentationRetentionEvidence evidence;
			evidence.analysisValid = evidence.presentationValid = true;
			evidence.excludedTop.barPixels = scope.top;
			evidence.excludedTop.blackFraction = 0.2;
			evidence.excludedTop.continuity = 0.4;
			evidence.excludedTop.lumaP90 = 300;
			auto one = ConfirmOutwardPictureTransition({},scope,larger,evidence,2,10);
			auto two = ConfirmOutwardPictureTransition(one.state,scope,larger,evidence,2,11);
			Assert::AreEqual(2u,two.state.confirmations);
            Assert::AreEqual("proof-pending",one.diagnosticReason);
            Assert::AreEqual("proof-pending",two.diagnosticReason);
            Assert::AreEqual("proof-restarted",ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,2,13).diagnosticReason);
            Assert::AreEqual("proof-restarted",ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,3,12).diagnosticReason);
            Assert::AreEqual("proof-restarted",ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,2,9).diagnosticReason);
			Assert::AreEqual(1u,ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,2,13).state.confirmations);
			Assert::AreEqual(1u,ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,3,12).state.confirmations);
			Assert::AreEqual(1u,ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,2,9).state.confirmations);
			evidence.analysisValid = false;
			Assert::IsFalse(ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,2,12).authoritative);
			evidence.analysisValid = true; evidence.presentationValid = false;
			Assert::IsFalse(ConfirmOutwardPictureTransition(two.state,scope,larger,evidence,2,12).authoritative);
		}

		TEST_METHOD(DeferredHistoryCannotPublishOrBankConfirmations)
		{
			const auto scope = TrustedScopeCrop().geometry;
			auto taller = scope; taller.top = 100; taller.bottom = 2060;
			ActivePictureTransitionModel model;
			for (uint64_t seq = 1; seq <= 4; ++seq)
				model.Observe({taller, seq, true, ActivePictureClassification::BAR_CROP_TRUSTED, 24});
			for (uint64_t seq = 5; seq <= 120; ++seq)
				model.Observe({scope, seq, true, ActivePictureClassification::BAR_CROP_TRUSTED, 24});
			for (uint64_t seq = 121; seq <= 150; ++seq)
			{
				ActivePictureObservation observation{taller, seq, true, ActivePictureClassification::PROVISIONAL, 24};
				observation.transitionDeferred = true;
				const auto d = model.Observe(observation);
				Assert::IsFalse(d.publish || d.clearTransition || d.knownTrustedGeometryReacquired);
				Assert::AreEqual(scope.top, d.stableBounds.top);
			}
			// Natural uncertainty is still allowed to reacquire remembered geometry,
			// but needs fresh observations after the explicit veto ends.
			Assert::IsFalse(model.Observe({taller, 151, true, ActivePictureClassification::PROVISIONAL, 24}).publish);
			Assert::IsTrue(model.Observe({taller, 152, true, ActivePictureClassification::PROVISIONAL, 24}).publish);
		}

		TEST_METHOD(MixedEdgeExpansionRequiresPictureEvidenceAndCanSupersedeSubtitle)
		{
			TransitionAdmissionInput input;
			input.trustedGeometry = {188,364,3652,1796,3840,2160,2.419,ActivePictureBounds::BarAxes::BOTH};
			input.presentationBeforeObservation = input.trustedGeometry;
			input.evidence.available = input.trustedGeometryAvailable = input.compatiblePresentation = true;
			input.evidence.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.evidence.trustedBounds = {192,0,3648,2160,3840,2160,1.6,ActivePictureBounds::BarAxes::LEFT_RIGHT};
			input.outwardCandidate = input.evidence.trustedBounds;
			input.trustedGeneration = input.sourceGeneration = input.presentationEvidenceGeneration = 2;
			input.presentation.action = VerticalBarPresentationAction::TRANSLATE;
			input.retention.analysisValid = input.retention.presentationValid = true;
			input.sourceSequence = 100;
			const auto overlay = EvaluateTransitionAdmission(input);
			Assert::IsTrue(overlay.outward.outwardTransition);
			Assert::IsTrue(overlay.observation.transitionDeferred);
			// Real picture on both newly exposed vertical edges is not locked out
			// merely because a subtitle action was active on the preceding frame.
			input.retention.excludedTop.barPixels = 364;
			input.retention.excludedTop.blackFraction = 0.2;
			input.retention.excludedTop.continuity = 0.4;
			input.retention.excludedTop.lumaP90 = 300;
			input.retention.excludedBottom = input.retention.excludedTop;
			for (uint64_t seq=101; seq<=103; ++seq)
			{
				input.sourceSequence = seq;
				const auto d = EvaluateTransitionAdmission(input);
				Assert::AreEqual(seq != 103, d.observation.transitionDeferred);
				input.previousOutward = d.outward.state;
			}
		}

		TEST_METHOD(AsymmetricExpansionChecksOnlyNewlyExposedEdges)
		{
			const auto scope = TrustedScopeCrop().geometry;
			auto candidate = scope; candidate.top = 100; candidate.bottom -= 4;
			ActivePicturePresentationRetentionEvidence evidence;
			evidence.analysisValid = evidence.presentationValid = true;
			evidence.excludedTop.barPixels = scope.top;
			evidence.excludedTop.blackFraction = 0.2;
			evidence.excludedTop.continuity = 0.4;
			evidence.excludedTop.lumaP90 = 300;
			OutwardPictureConfirmationState state;
			for (uint64_t seq=1; seq<=3; ++seq)
			{
				const auto d = ConfirmOutwardPictureTransition(state, scope, candidate, evidence, 2, seq);
				Assert::IsTrue(d.outwardTransition && d.broadOpposingPicture);
				Assert::AreEqual(seq == 3, d.authoritative);
				state = d.state;
			}
			candidate.left = 4; // no new left pixels; still only an upper expansion
			Assert::IsTrue(ConfirmOutwardPictureTransition({}, scope, candidate, evidence, 2, 4).broadOpposingPicture);
		}

		TEST_METHOD(RecordedSubtitleOnsetMustNotReacquireFullHeightOrStrandRecovery)
		{
			// VP-0189 recording 2026-09-17 09-35-45.mp4, event 11:
			// 90015 starts subtitle confirmation; 90016 publishes remembered
			// pillarbox and withdraws crop. This is a policy-input fixture, not a
			// raw-pixel replay of the desktop recording. Repeated middle frames
			// below sustain the logged evidence rather than invent missing scans.
			std::string failures;
			for (double rate : {23.976, 24.0, 59.94, 60.0})
			{
				const ActivePictureBounds scope{188, 364, 3652, 1796, 3840, 2160,
					3464.0 / 1432.0, ActivePictureBounds::BarAxes::BOTH};
				const ActivePictureBounds pillar{188, 0, 3648, 2160, 3840, 2160,
					3460.0 / 2160.0, ActivePictureBounds::BarAxes::LEFT_RIGHT};
				auto rawPillar = pillar; rawPillar.left = 192; rawPillar.right = 3652;
				ActivePictureTransitionModel model;
				ActivePictureTransitionDecision seeded;
				for (uint64_t frame = 1; frame <= 4; ++frame)
					seeded = model.Observe({pillar, frame, true, ActivePictureClassification::BAR_CROP_TRUSTED, rate});
				const uint64_t seedEnd = 8 + static_cast<uint64_t>(std::ceil(5 * rate));
				for (uint64_t frame = 5; frame <= seedEnd; ++frame)
					seeded = model.Observe({scope, frame, true, ActivePictureClassification::BAR_CROP_TRUSTED, rate});
				Assert::IsTrue(seeded.stable);
				Assert::AreEqual(scope.top, seeded.stableBounds.top);
				ActivePictureBounds logical = scope;
				OutwardPictureConfirmationState outward;
				VerticalTranslationConfirmationState translationConfirmation;
				VerticalBarPresentationState subtitlePresentation;
				PresentationRecoveryState recoveryState;
				uint64_t firstWrongLogical = 0, firstFullRaster = 0;
				unsigned fullRasterDuringSubtitle = 0, clippedSubtitles = 0;
				unsigned confirmedSubtitleFrames = 0, boundedCandidatesHeldFull = 0;
				bool recoveredAfterRemoval = false;
				const uint64_t subtitleFrames = static_cast<uint64_t>(std::ceil(11.44 * rate));
				const uint64_t recoveryFrames = static_cast<uint64_t>(std::ceil(6 * rate));
				for (uint64_t offset = 0; offset < subtitleFrames + recoveryFrames; ++offset)
				{
					const uint64_t seq = 90015 + offset;
					const bool subtitle = offset < subtitleFrames;
					const auto before = logical;
					const auto raw = subtitle ? rawPillar : scope;
					ActivePictureEvidence evidence;
					evidence.available = true;
					evidence.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
					evidence.trustedBounds = subtitle ? pillar : scope;
					ActivePicturePresentationRetentionEvidence retention;
					retention.analysisValid = retention.presentationValid = true;
					retention.activePicture = evidence;
					retention.excludedBandsPixelSafe = !subtitle;
					retention.outwardVisibleBoundsAvailable = subtitle;
					retention.outwardVisibleBounds = before;
					retention.outwardVisibleBounds.right = std::max(before.right, 3652);
					retention.outwardVisibleBounds.bottom = std::max(before.bottom, 1846);
					// Localized bottom overlay, not broad opposing picture.
					retention.excludedBottom.barPixels = 364;
					retention.excludedBottom.blackFraction = 0.97;
					retention.excludedBottom.continuity = 0.98;
					retention.excludedBottom.lumaP90 = 64;
					TransitionAdmissionInput admissionInput;
					admissionInput.evidence = evidence;
					admissionInput.outwardCandidate = raw;
					admissionInput.presentationBeforeObservation = before;
					admissionInput.trustedGeometry = logical;
					admissionInput.trustedGeometryAvailable = admissionInput.compatiblePresentation = true;
					admissionInput.trustedGeneration = admissionInput.sourceGeneration = 2;
					admissionInput.sourceSequence = seq;
					admissionInput.framesPerSecond = rate;
					admissionInput.presentation = subtitlePresentation;
					admissionInput.presentationEvidenceGeneration = 2;
					admissionInput.previousOutward = outward;
					admissionInput.retention = retention;
					const auto admission = EvaluateTransitionAdmission(admissionInput);
					outward = admission.outward.state;
					const auto transition = model.Observe(admission.observation);
					if (transition.publish && transition.stable) logical = transition.bounds;
					else if (transition.stable) logical = transition.stableBounds;
					if (subtitle && logical.top == 0 && !firstWrongLogical) firstWrongLogical = seq;

					// The renderer analyzes vertical bars after logical publication.
					// Once the model publishes full-height geometry, there are no
					// vertical bars for that dense scan to confirm against.
					VerticalTranslationConfirmationInput translationInput;
					translationInput.previous = translationConfirmation;
					translationInput.sourceSequence = seq;
					if (subtitle && logical.top > 0 && logical.bottom < 2160)
					{
						translationInput.observed.action = VerticalBarPresentationAction::TRANSLATE;
						translationInput.observed.translationPixels = 84;
					}
					translationInput.acceptedTranslationActive = subtitlePresentation.action == VerticalBarPresentationAction::TRANSLATE;
					translationInput.acceptedTranslationPixels = subtitlePresentation.translationPixels;
					const auto translation = ConfirmVerticalTranslation(translationInput);
					translationConfirmation = translation.state;
					subtitlePresentation.action = translation.effective.action;
					subtitlePresentation.translationPixels = translation.effective.translationPixels;
					if (subtitlePresentation.action == VerticalBarPresentationAction::TRANSLATE) ++confirmedSubtitleFrames;

					VerticalBarPresentationResolutionInput routingInput;
					routingInput.detailedAction = subtitlePresentation.action;
					routingInput.translationPixels = subtitlePresentation.translationPixels;
					routingInput.genericUpperExpansion = subtitle && raw.top < logical.top;
					routingInput.genericLowerExpansion = subtitle && raw.bottom > logical.bottom;
					routingInput.genericUpperBound = raw.top;
					routingInput.genericLowerBound = raw.bottom;
					routingInput.authoritativeTop = logical.top;
					routingInput.authoritativeBottom = logical.bottom;
					routingInput.rasterHeight = 2160;
					routingInput.denseVerticalArbitrationEnabled = true;
					const auto routing = ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(routingInput));
					PresentationEnvelopeGeometryInput envelopeInput;
					envelopeInput.trustedPicture = logical;
					envelopeInput.observedContent = raw;
					envelopeInput.observedContentAvailable = subtitle;
					envelopeInput.expandLeft = raw.left < logical.left;
					envelopeInput.expandTop = routing.fitActive && raw.top < logical.top;
					envelopeInput.expandRight = raw.right > logical.right;
					envelopeInput.expandBottom = routing.fitActive && raw.bottom > logical.bottom;
					const auto envelope = BuildPresentationEnvelope(envelopeInput);
					Input crop = TrustedScopeCrop();
					crop.geometry = logical;
					crop.geometrySourceGeneration = crop.frameSourceGeneration = 2;
					crop.frameSourceSequence = seq;
					crop.latestObservationClassification = evidence.classification;
					const bool samplingSafe = IsPixelSafeCropReaffirmation(logical, evidence.trustedBounds, retention.excludedBandsPixelSafe);
					crop.latestObservationSupportsCrop = samplingSafe ||
						(logical.left <= raw.left && logical.top <= raw.top && logical.right >= raw.right && logical.bottom >= raw.bottom);
					crop.barCropRefinementPending = !crop.latestObservationSupportsCrop;
					// Derive conflict from evidence and published geometry; never force
					// a full-raster decision or an active recovery state in the fixture.
					crop.barCropRefinementHorizontalConflict = HasHorizontalCropRefinementConflict(
						subtitle && raw.left < logical.left, subtitle && raw.right > logical.right,
						true, true, samplingSafe, raw, logical);
					crop.frameLocalPresentationRetentionEvaluated = true;
					crop.frameLocalPresentationRetentionSafe = retention.excludedBandsPixelSafe;
					crop.verticalTranslationConfirmationPending = translation.pending;
					crop.verticalTranslationActive = routing.translationActive;
					crop.presentationFailOpen = routing.failOpen;
					crop.verticalTranslationPixels = subtitlePresentation.translationPixels;
					crop.verticalTranslationBase = before;
					crop.verticalTranslationSourceGeneration = 2;
					crop.outwardPresentationActive = crop.outwardExpansionAvailable = envelope.valid && envelope.expanded;
					crop.outwardExpansion = envelope.bounds;
					crop.outwardExpansionSourceGeneration = 2;
					crop.currentVisibleBoundsAvailable = subtitle;
					crop.currentVisibleBase = before;
					crop.currentVisibleBounds = retention.outwardVisibleBounds;
					crop.currentVisibleBounds.left = std::min(crop.currentVisibleBounds.left, raw.left);
					crop.currentVisibleBounds.top = std::min(crop.currentVisibleBounds.top, raw.top);
					crop.currentVisibleBounds.right = std::max(crop.currentVisibleBounds.right, raw.right);
					crop.currentVisibleBounds.bottom = std::max(crop.currentVisibleBounds.bottom, raw.bottom);
					crop.currentVisibleSourceGeneration = 2;
					crop.currentVisibleSourceSequence = seq;
					const auto candidate = Evaluate(crop);
					PresentationRecoveryInput recovery;
					recovery.previous = recoveryState;
					recovery.crop = crop; recovery.candidate = candidate;
					recovery.measurementCurrent = recovery.retentionEvaluated = recovery.observationAvailable = recovery.nearBlackEvaluated = true;
					recovery.retentionBounds = before;
					recovery.retentionSourceGeneration = 2; recovery.retentionSourceSequence = seq;
					recovery.observation = raw; recovery.observedTrustedCrop = evidence.trustedBounds;
					recovery.observationClassification = evidence.classification;
					recovery.excludedBandsPixelSafe = retention.excludedBandsPixelSafe;
					recovery.framesPerSecond = rate; recovery.presentationEpoch = 1;
					recovery.currentTick = 1000 + static_cast<uint64_t>(offset * 1000 / rate);
					const auto result = EvaluatePresentationRecovery(recovery);
					recoveryState = result.state;
					if (!result.presentation.applyCrop && !firstFullRaster) firstFullRaster = seq;
					if (subtitle && offset >= 8)
					{
						if (!result.presentation.applyCrop) ++fullRasterDuringSubtitle;
						if (candidate.applyCrop && !result.presentation.applyCrop) ++boundedCandidatesHeldFull;
						if (result.presentation.sourceBounds.bottom < 1846) ++clippedSubtitles;
					}
					if (!subtitle && !result.state.active && result.presentation.applyCrop &&
						result.presentation.sourceBounds.top == scope.top && result.presentation.sourceBounds.bottom == scope.bottom)
						recoveredAfterRemoval = true;
				}
				const std::string trace = "rate=" + std::to_string(rate) +
					" first_wrong_logical=" + std::to_string(firstWrongLogical) +
					" first_full_raster=" + std::to_string(firstFullRaster) +
					" subtitle_full_frames=" + std::to_string(fullRasterDuringSubtitle) +
					" fit_candidates_held_full=" + std::to_string(boundedCandidatesHeldFull) +
					" confirmed_subtitle_frames=" + std::to_string(confirmedSubtitleFrames) +
					" clipped_subtitle_frames=" + std::to_string(clippedSubtitles) +
					" recovered_after_removal=" + std::to_string(recoveredAfterRemoval) + "\n";
				Logger::WriteMessage(trace.c_str());
				if (firstWrongLogical || fullRasterDuringSubtitle || clippedSubtitles || !recoveredAfterRemoval)
					failures += trace;
			}
			Assert::IsTrue(failures.empty(), std::wstring(failures.begin(), failures.end()).c_str());
		}

		TEST_METHOD(LiveFourPixelCoarseEnvelopeDoesNotWithdrawPixelSafePillarboxCrop)
		{
			Input input = TrustedScopeCrop();
			input.geometry = {192, 0, 3652, 2160, 3840, 2160,
				3460.0 / 2160.0, ActivePictureBounds::BarAxes::LEFT_RIGHT};
			auto observed = input.geometry; observed.left = 188;
			auto envelope = observed; envelope.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			for (int i = 0; i < 120; ++i)
			{
				observed.left = envelope.left = i % 2 ? 188 : 192;
				const bool reaffirmed = IsPixelSafeSamplingEnvelope(input.geometry, observed, envelope, true);
				Assert::IsTrue(reaffirmed);
				input.barCropRefinementHorizontalConflict = envelope.left < input.geometry.left && !reaffirmed;
				const auto crop = Evaluate(input);
				Assert::IsTrue(crop.applyCrop);
				Assert::AreEqual(192, crop.sourceBounds.left);
				Assert::AreEqual(3652, crop.sourceBounds.right);
				PresentationRecoveryInput recovery;
				recovery.crop = input; recovery.candidate = crop;
				Assert::IsFalse(EvaluatePresentationRecovery(recovery).started);
			}
		}

		TEST_METHOD(CoarseSamplingReaffirmationCannotHidePixelsOrMaterialGeometry)
		{
			const auto trusted = TrustedScopeCrop().geometry;
			auto observed = trusted; observed.bottom += 4;
			auto envelope = observed; envelope.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			Assert::IsTrue(IsPixelSafeSamplingEnvelope(trusted, observed, envelope, true));
			Assert::IsFalse(IsPixelSafeSamplingEnvelope(trusted, observed, envelope, false));
			envelope.bottom += 8;
			Assert::IsFalse(IsPixelSafeSamplingEnvelope(trusted, observed, envelope, true));
			envelope = observed; envelope.top -= 8; // aggregate size exceeds sampling tolerance
			Assert::IsFalse(IsPixelSafeSamplingEnvelope(trusted, observed, envelope, true));
			envelope = observed; observed.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			Assert::IsFalse(IsPixelSafeSamplingEnvelope(trusted, observed, envelope, true));
			observed = trusted; envelope.rasterWidth = 1920;
			Assert::IsFalse(IsPixelSafeSamplingEnvelope(trusted, observed, envelope, true));
		}

		TEST_METHOD(WomenInBlueCurrentBoundedHorizontalContentUsesFitInsteadOfFullRaster)
		{
			Input input = TrustedScopeCrop();
			input.geometry = { 192, 384, 3648, 1780, 3840, 2160,
				3456.0 / 1396.0, ActivePictureBounds::BarAxes::BOTH };
			input.frameSourceSequence = 13857;
			input.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.barCropRefinementHorizontalConflict = true;
			input.outwardPresentationActive = input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry; input.outwardExpansion.right = 3722;
			input.outwardExpansionSourceGeneration = 7;
			// Without a complete current pixel certificate, preserve fail-open.
			AssertFullRaster(Evaluate(input));
			input.currentVisibleBoundsAvailable = true;
			input.currentVisibleBase = input.geometry;
			input.currentVisibleBounds = input.geometry; input.currentVisibleBounds.right = 3698;
			input.currentVisibleSourceSequence = 13857; input.currentVisibleSourceGeneration = 7;
			const auto fit = Evaluate(input);
			Assert::IsTrue(fit.applyCrop && fit.outwardExpanded && fit.horizontalExpansionPixelBounded);
			Assert::AreEqual(3722, fit.sourceBounds.right);
			Assert::AreEqual(384, fit.sourceBounds.top);
			Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT), int(fit.owner));
			PresentationRecoveryInput recovery;
			recovery.crop = input; recovery.candidate = fit;
			Assert::IsFalse(EvaluatePresentationRecovery(recovery).started);
			// A previously armed unresolved event still needs its inward proof.
			recovery.previous.active = true; recovery.previous.sourceGeneration = 7;
			recovery.previous.trustedCrop = input.geometry;
			AssertFullRaster(EvaluatePresentationRecovery(recovery).presentation);
			for (int failure = 0; failure < 11; ++failure)
			{
				auto bad = input;
				switch (failure)
				{
				case 0: bad.currentVisibleSourceSequence--; break;
				case 1: bad.currentVisibleSourceGeneration--; break;
				case 2: bad.currentVisibleBase.top += 4; break;
				case 3: bad.currentVisibleBounds.right = 3730; break;
				case 4: bad.currentVisibleBounds.bottom = 1800; break;
				case 5: bad.latestObservationClassification = ActivePictureClassification::PROVISIONAL; break;
				case 6: bad.outwardExpansionSourceGeneration--; break;
				case 7: bad.outwardExpansion.right = 3721; break;
				case 8: bad.presentationFailOpen = true; break;
				case 9: bad.fullRasterPresentationAuthoritative = true; break;
				case 10: bad.verticalTranslationActive = true; break;
				}
				AssertFullRaster(Evaluate(bad));
			}
		}

		TEST_METHOD(NearBlackRecoveryUsesTheSameSamplingEquivalenceAndPixelVeto)
		{
			for (bool safe : { false, true })
			{
				NearBlackPresentationEpisodeInput input;
				input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
				input.previous.entryTrustedCropAvailable = true;
				input.previous.entryTrustedCrop = TrustedScopeCrop().geometry;
				input.sourceGeneration = input.previous.sourceGeneration = input.retentionSourceGeneration = 7;
				input.presentationEpoch = input.previous.presentationEpoch = input.reacquiredPresentationEpoch = 2;
				input.previous.fullRasterStartedSourceSequence = 100;
				input.reacquiredSourceSequence = 198; input.reacquiredSourceGeneration = 7;
				input.measurementCurrent = input.nearBlackEvaluated = input.retentionEvaluated = true;
				input.currentObservationAvailable = input.knownTrustedGeometryReacquired = input.reacquisitionIsCurrentAssociation = true;
				input.reacquiredTrustedGeometry = input.retentionBounds = input.previous.entryTrustedCrop;
				input.reacquiredTrustedClassification = input.currentObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
				ActivePictureEvidence raw;
				raw.available = true; raw.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
				raw.trustedBounds = input.previous.entryTrustedCrop; raw.trustedBounds.bottom += 4;
				const auto constrained = ConstrainNearBlackCropAcquisition(raw, true);
				Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL), int(constrained.classification));
				input.currentObservation = constrained.proposedBounds;
				input.currentObservationClassification = raw.classification;
				input.retentionExcludedBandsPixelSafe = safe;
				input.retentionSafe = false; // strict containment is false at the old edge
				input.framesPerSecond = 24;
				for (uint64_t seq = 200; seq < 207; ++seq)
				{
					input.sourceSequence = input.retentionSourceSequence = seq;
					const auto d = EvaluateNearBlackPresentationEpisode(input);
					Assert::AreEqual(safe && seq == 206, d.releasedToTrustedCrop);
					input.previous = d.state;
				}
			}
		}

		TEST_METHOD(SamplingReaffirmationRequiresSafeBandsAndTheSameBarContract)
		{
			const auto trusted = TrustedScopeCrop().geometry;
			auto observed = trusted;
			observed.bottom += 4;
			Assert::IsTrue(IsPixelSafeCropReaffirmation(trusted, observed, true));
			Assert::IsFalse(IsPixelSafeCropReaffirmation(trusted, observed, false));
			observed.bottom += 8;
			Assert::IsFalse(IsPixelSafeCropReaffirmation(trusted, observed, true));
			observed = trusted; observed.top -= 8; observed.bottom += 8;
			Assert::IsFalse(IsPixelSafeCropReaffirmation(trusted, observed, true));
			observed = trusted; observed.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			Assert::IsFalse(IsPixelSafeCropReaffirmation(trusted, observed, true));
			observed = trusted; observed.rasterWidth = 1920;
			Assert::IsFalse(IsPixelSafeCropReaffirmation(trusted, observed, true));
		}

		TEST_METHOD(WomenInBlueSamplingNoiseResolvesInspectionAndRecoveryWithoutNewAspect)
		{
			for (double rate : { 23.976, 24.0, 59.94, 60.0 })
			{
				PresentationRecoveryInput input;
				input.crop = TrustedScopeCrop();
				input.crop.geometry = { 0, 208, 3840, 1948, 3840, 2160,
					3840.0 / 1740.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
				auto admission = AdmitCropPresentation({}, input.crop, Evaluate(input.crop), 0).state;
				ActivePictureTransitionModel model;
				for (uint64_t frame = 1; frame <= 4; ++frame)
					model.Observe({ input.crop.geometry, frame, true,
						ActivePictureClassification::BAR_CROP_TRUSTED, rate });
				input.previous.active = true;
				input.previous.sourceGeneration = input.retentionSourceGeneration = 7;
				input.previous.trustedCrop = input.retentionBounds = input.crop.geometry;
				input.previous.lastSourceSequence = 4;
				input.measurementCurrent = input.retentionEvaluated = input.nearBlackEvaluated = true;
				input.excludedBandsPixelSafe = input.observationAvailable = true;
				input.framesPerSecond = rate;
				input.observation = input.observedTrustedCrop = input.crop.geometry;
				input.observation.bottom = input.observedTrustedCrop.bottom = 1952;
				input.observation.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
				input.observationClassification = input.crop.latestObservationClassification =
					ActivePictureClassification::BAR_CROP_TRUSTED;
				VerticalInspectionBridgeInput bridge;
				bridge.previous.active = bridge.previous.failOpenLatched = true;
				bridge.sourceGeneration = bridge.previous.sourceGeneration = 7;
				bridge.trustedBase = bridge.previous.trustedBase = input.crop.geometry;
				const unsigned required = rate < 30 ? 7 : 16;
				for (unsigned n = 1; n <= required; ++n)
				{
					input.crop.frameSourceSequence = input.retentionSourceSequence = bridge.sourceSequence = n + 4;
					const auto transition = model.Observe({ input.observedTrustedCrop, n + 4, true,
						ActivePictureClassification::BAR_CROP_TRUSTED, rate });
					Assert::IsFalse(transition.publish);
					Assert::AreEqual(1948, transition.stableBounds.bottom);
					input.crop.latestObservationSupportsCrop = IsPixelSafeCropReaffirmation(
						input.crop.geometry, input.observedTrustedCrop, input.excludedBandsPixelSafe);
					bridge.cropAuthorityResolved = input.crop.latestObservationSupportsCrop;
					const auto inspection = UpdateVerticalInspectionBridge(bridge);
					bridge.previous = inspection.state;
					input.crop.presentationFailOpen = inspection.state.failOpenLatched;
					input.candidate = Evaluate(input.crop);
					const auto d = EvaluatePresentationRecovery(input);
					Assert::IsTrue(d.samplingReaffirmed);
					Assert::AreEqual(n, d.samples);
					Assert::AreEqual(n == required, d.presentation.applyCrop);
					const auto presented = AdmitCropPresentation(admission, input.crop, d.presentation, 0);
					Assert::AreEqual(d.presentation.applyCrop, presented.presentation.applyCrop);
					Assert::IsFalse(presented.blocked);
					admission = presented.state;
					if (d.presentation.applyCrop)
					{
						Assert::AreEqual(1948, d.presentation.sourceBounds.bottom);
						Assert::AreEqual(input.crop.geometry.aspectRatio, d.presentation.sourceBounds.aspectRatio);
					}
					input.previous = d.state;
				}
			}
		}

		TEST_METHOD(SamplingNoiseCannotBypassUnsafeStaleProvisionalOrFailOpenRecovery)
		{
			PresentationRecoveryInput good;
			good.crop = TrustedScopeCrop();
			good.crop.frameSourceSequence = good.retentionSourceSequence = 50;
			good.previous.active = true;
			good.previous.sourceGeneration = good.retentionSourceGeneration = 7;
			good.previous.trustedCrop = good.retentionBounds = good.crop.geometry;
			good.previous.lastSourceSequence = 49; good.previous.samples = 6;
			good.framesPerSecond = 24;
			good.measurementCurrent = good.retentionEvaluated = good.excludedBandsPixelSafe = true;
			good.observationAvailable = good.nearBlackEvaluated = true;
			good.observation = good.observedTrustedCrop = good.crop.geometry;
			good.observation.bottom += 4; good.observedTrustedCrop.bottom += 4;
			good.observationClassification = good.crop.latestObservationClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			for (int failure = 0; failure < 9; ++failure)
			{
				auto input = good;
				switch (failure)
				{
				case 0: input.excludedBandsPixelSafe = false; break;
				case 1: input.retentionSourceSequence = 49; break;
				case 2: input.retentionSourceGeneration = 6; break;
				case 3: input.observationClassification = ActivePictureClassification::PROVISIONAL; break;
				case 4: input.globalNearBlack = true; break;
				case 5: input.crop.presentationFailOpen = true; break;
				case 6: input.observation.bottom += 8; input.observedTrustedCrop.bottom += 8; break;
				case 7: input.crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL; break;
				case 8: input.crop.barCropRefinementHorizontalConflict = true; break;
				}
				input.candidate = Evaluate(input.crop);
				const auto d = EvaluatePresentationRecovery(input);
				AssertFullRaster(d.presentation);
				Assert::IsFalse(d.released);
				Assert::AreEqual(0u, d.samples);
			}
		}

		TEST_METHOD(ReceivedLateBurstCannotReenterThroughUnsafeTrustedRefinementOrNearBlack)
		{
			PresentationRecoveryInput input;
			input.crop = TrustedScopeCrop();
			input.measurementCurrent = input.retentionEvaluated = input.nearBlackEvaluated = true;
			input.retentionSourceGeneration = 7;
			input.retentionBounds = input.observation = input.crop.geometry;
			input.observationAvailable = true;
			input.crop.frameLocalPresentationRetentionEvaluated = true;
			const uint64_t sequences[] = { 86284, 86285, 86286, 86287, 86288, 86289, 86291, 86298 };
			for (unsigned i = 0; i < 8; ++i)
			{
				input.crop.frameSourceSequence = input.retentionSourceSequence = sequences[i];
				input.crop.latestObservationSupportsCrop = i == 0 || i == 5;
				input.crop.barCropRefinementPending = i == 1 || i == 3;
				input.crop.latestObservationClassification =
					(input.crop.latestObservationSupportsCrop || input.crop.barCropRefinementPending)
					? ActivePictureClassification::BAR_CROP_TRUSTED : ActivePictureClassification::PROVISIONAL;
				input.crop.latestObservationIsProvisional =
					input.crop.latestObservationClassification == ActivePictureClassification::PROVISIONAL;
				input.crop.nearBlackEpisodeRetainCrop = i == 7;
				input.globalNearBlack = i == 7;
				input.excludedBandsPixelSafe = i == 7;
				input.crop.frameLocalPresentationRetentionSafe = i == 7;
				input.candidate = Evaluate(input.crop);
				const auto decision = EvaluatePresentationRecovery(input);
				Assert::AreEqual(i < 2, decision.presentation.applyCrop);
				if (i >= 2) Assert::IsTrue(decision.state.active);
				input.previous = decision.state;
			}
		}

		TEST_METHOD(RecoveryArmsAfterHorizontalConflictOrExplicitFailOpenWithoutOverridingThem)
		{
			for (bool horizontal : { false, true })
			{
				PresentationRecoveryInput input;
				input.crop = TrustedScopeCrop();
				input.crop.frameSourceSequence = 90;
				input.crop.presentationFailOpen = !horizontal;
				input.crop.barCropRefinementHorizontalConflict = horizontal;
				input.candidate = Evaluate(input.crop);
				const auto decision = EvaluatePresentationRecovery(input);
				Assert::IsTrue(decision.started);
				AssertFullRaster(decision.presentation);
			}
		}

		TEST_METHOD(NearBlackEntryProofCannotCrossProfileEpochOrCountOlderFrames)
		{
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = input.sourceGeneration = 1;
			input.previous.presentationEpoch = input.presentationEpoch = 2;
			input.previous.entryTrustedCropAvailable = true;
			input.previous.entryTrustedCrop = TrustedScopeCrop().geometry;
			input.previous.lastEvaluatedSourceSequence = 100;
			input.previous.revalidationLastSourceSequence = 100;
			input.previous.revalidationSamples = 3;
			input.sourceSequence = 99;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(3u, decision.state.revalidationSamples);
			Assert::IsTrue((decision.revalidationGates & RECOVERY_REPEAT) != 0);
			input.previous = decision.state;
			input.sourceSequence = 101;
			input.presentationEpoch = 3;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsFalse(decision.releasedToTrustedCrop);
			Assert::IsFalse(decision.state.entryTrustedCropAvailable);
			Assert::AreEqual(0u, decision.state.revalidationSamples);
			Assert::AreEqual(uint64_t(3), decision.state.presentationEpoch);
		}

		TEST_METHOD(RecoveryDoesNotChangeOrdinaryAcquisitionOrFullRasterTiming)
		{
			PresentationRecoveryInput input;
			input.crop = TrustedScopeCrop();
			input.candidate = Evaluate(input.crop);
			auto d = EvaluatePresentationRecovery(input);
			Assert::IsFalse(d.state.active);
			Assert::IsTrue(d.presentation.applyCrop);
			input.previous.active = true;
			input.previous.sourceGeneration = 7;
			input.previous.trustedCrop = input.crop.geometry;
			input.crop.fullRasterPresentationAuthoritative = true;
			input.candidate = Evaluate(input.crop);
			d = EvaluatePresentationRecovery(input);
			AssertFullRaster(d.presentation);
			Assert::IsFalse(d.state.active);
			Assert::AreEqual(static_cast<unsigned>(RECOVERY_FULL_AUTHORITY), d.gates);
		}

		TEST_METHOD(RecoveryStopsAlternatingPixelSafeTrustedAndRefinementOwners)
		{
			PresentationRecoveryInput input;
			input.crop = TrustedScopeCrop();
			input.crop.latestObservationSupportsCrop = false;
			input.crop.latestObservationIsProvisional = true;
			input.crop.frameLocalPresentationRetentionEvaluated = true;
			input.measurementCurrent = input.retentionEvaluated = true;
			input.nearBlackEvaluated = input.observationAvailable = true;
			input.observation = input.retentionBounds = input.crop.geometry;
			input.retentionSourceGeneration = 7;
			for (uint64_t seq = 1; seq <= 240; ++seq)
			{
				input.crop.frameSourceSequence = input.retentionSourceSequence = seq;
				input.crop.frameLocalPresentationRetentionSafe = seq % 2 == 0;
				input.excludedBandsPixelSafe = seq % 2 == 0;
				// Received 00:39 burst: a trusted/refinement label can appear while
				// excluded pixels still conflict. Neither resets this event.
				input.crop.latestObservationSupportsCrop = seq % 8 == 4;
				input.crop.barCropRefinementPending = seq % 8 == 6;
				input.candidate = Evaluate(input.crop);
				auto d = EvaluatePresentationRecovery(input);
				AssertFullRaster(d.presentation);
				Assert::IsTrue(d.state.active);
				input.previous = d.state;
			}
		}

		TEST_METHOD(RecoveryRequiresAdjacentQuarterSecondProofAtSourceFrameRate)
		{
			for (bool injectMovement : {false,true})
            for (double hz : { 23.976, 24.0, 59.94, 60.0 })
			{
				PresentationRecoveryInput input;
				input.crop = TrustedScopeCrop();
				auto admission = AdmitCropPresentation({}, input.crop, Evaluate(input.crop), 0).state;
				input.crop.frameSourceSequence = injectMovement ? 90 : 100;
				input.crop.latestObservationSupportsCrop = false;
				input.crop.latestObservationIsProvisional = true;
				input.candidate = Evaluate(input.crop);
				input.framesPerSecond = hz;
				auto d = EvaluatePresentationRecovery(input);
				Assert::IsTrue(d.started);
				input.previous = d.state;
				if (injectMovement)
				{
					MovingPictureTransitionState moving;
					for (unsigned index=0;index<10;++index)
					{
						const uint64_t seq=91+index;
						auto observation=MovingRecoveryObservation(input.crop.geometry,7,seq,index,hz);
						moving=ObserveMovingPictureTransition(moving,{7,seq,seq,seq*417083,11,0,17},observation);
						input.crop.frameSourceSequence=seq;
						input.crop.movingPictureTransition=HasCurrentMovingPictureTransition(moving,7,input.crop.frameSourceSequence);
						input.candidate=Evaluate(input.crop);
						d=EvaluatePresentationRecovery(input); input.previous=d.state;
						Assert::IsTrue(d.state.active,L"Moving presentation cannot erase an existing recovery obligation.");
						Assert::IsFalse(d.presentation.applyCrop);
						Assert::AreEqual(0u,d.samples,L"Moving pixels cannot count as safe-band recovery samples.");
						Assert::IsFalse(input.crop.fullRasterPresentationAuthoritative);
					}
					Assert::IsTrue(moving.active);
					auto stopped=MovingRecoveryObservation(input.crop.geometry,7,101,0,hz);
					stopped.evidence.trustedBounds=stopped.outwardCandidate=input.crop.geometry;
                    stopped.retention.excludedBandsPixelSafe=true;
					moving=ObserveMovingPictureTransition(moving,{7,101,101,101*417083,11,0,17},stopped);
                    CompleteMovingPictureTransition(moving,stopped,{});
					Assert::IsFalse(moving.active);
					input.crop.movingPictureTransition=HasCurrentMovingPictureTransition(moving,7,input.crop.frameSourceSequence);
				}
				input.crop.frameLocalPresentationRetentionSafe = true;
				input.crop.frameLocalPresentationRetentionEvaluated = true;
				input.measurementCurrent = input.retentionEvaluated = true;
				input.excludedBandsPixelSafe = true;
				input.observationAvailable = input.nearBlackEvaluated = true;
				input.observation = input.retentionBounds = input.crop.geometry;
				input.observation.top += 4;
				input.retentionSourceGeneration = 7;
				const unsigned required = hz < 30 ? 7 : 16;
				for (unsigned sample = 1; sample <= required; ++sample)
				{
					input.crop.frameSourceSequence = input.retentionSourceSequence = 100 + sample;
					input.candidate = Evaluate(input.crop);
					d = EvaluatePresentationRecovery(input);
					Assert::AreEqual(required, d.required);
					Assert::AreEqual(sample, d.samples);
					Assert::AreEqual(sample == required, d.presentation.applyCrop);
					const auto presented = AdmitCropPresentation(admission, input.crop, d.presentation, 0);
					Assert::AreEqual(d.presentation.applyCrop, presented.presentation.applyCrop);
					Assert::IsFalse(presented.blocked);
					admission = presented.state;
					input.previous = d.state;
				}
				Assert::IsTrue(d.released);
				Assert::AreEqual(input.crop.geometry.top, d.presentation.sourceBounds.top);
			}
		}

		TEST_METHOD(RecoveryRejectsRepeatsStaleEvidenceGapsAndContextChanges)
		{
			PresentationRecoveryInput input;
			input.crop = TrustedScopeCrop();
			input.crop.frameSourceSequence = input.retentionSourceSequence = 21;
			input.previous.active = true;
			input.previous.sourceGeneration = 7;
			input.previous.trustedCrop = input.crop.geometry;
			input.previous.lastSourceSequence = 20;
			input.previous.samples = 5;
			input.retentionSourceGeneration = 7;
			input.retentionBounds = input.observation = input.crop.geometry;
			input.measurementCurrent = input.retentionEvaluated = true;
			input.nearBlackEvaluated = input.observationAvailable = input.excludedBandsPixelSafe = true;
			input.candidate = Evaluate(input.crop);
			input.cadenceRepeat = true;
			auto d = EvaluatePresentationRecovery(input);
			Assert::AreEqual(5u, d.samples);
			Assert::IsTrue((d.gates & RECOVERY_REPEAT) != 0);
			AssertFullRaster(d.presentation);
			input.cadenceRepeat = false;
			input.retentionSourceSequence = 20;
			d = EvaluatePresentationRecovery(input);
			Assert::IsTrue(d.proofReset);
			Assert::AreEqual(0u, d.samples);
			input.crop.frameSourceSequence = input.retentionSourceSequence = 23;
			d = EvaluatePresentationRecovery(input);
			Assert::AreEqual(1u, d.samples);
			Assert::IsTrue((d.gates & RECOVERY_SEQUENCE_GAP) != 0);
			input.retentionSourceGeneration = 6;
			d = EvaluatePresentationRecovery(input);
			Assert::AreEqual(0u, d.samples);
			input.presentationEpoch = 2;
			d = EvaluatePresentationRecovery(input);
			Assert::IsTrue(d.ended);
			Assert::AreEqual(static_cast<unsigned>(RECOVERY_CONTEXT), d.gates);
		}

		TEST_METHOD(RecoveryPendingOwnersCannotBypassButConfirmedPresentationCan)
		{
			PresentationRecoveryInput input;
			input.crop = TrustedScopeCrop();
			input.crop.frameSourceSequence = 22;
			input.previous.active = true;
			input.previous.sourceGeneration = 7;
			input.previous.trustedCrop = input.crop.geometry;
			input.candidate = Evaluate(input.crop);
			for (auto owner : { DecisionOwner::TRUSTED_CROP, DecisionOwner::BAR_REFINEMENT,
				DecisionOwner::VERTICAL_INSPECTION, DecisionOwner::NEAR_BLACK_EPISODE,
				DecisionOwner::FIT_CONFIRMATION, DecisionOwner::TRANSLATION_CONFIRMATION })
			{
				input.candidate.owner = owner;
				AssertFullRaster(EvaluatePresentationRecovery(input).presentation);
			}
			input.candidate.owner = DecisionOwner::OUTWARD_FIT;
			input.confirmedPresentationResolved = true;
			auto d = EvaluatePresentationRecovery(input);
			Assert::IsTrue(d.released);
			Assert::IsTrue(d.presentation.applyCrop);
		}

		TEST_METHOD(ProfileTransitionRetainsOnlyCurrentTrustedSourceGeometry)
		{
			ProfileTransitionRetentionInput input;
			input.geometryAvailable = true;
			input.classification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.geometry = {
				0, 120, 3840, 2040, 3840, 2160, 2.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.geometrySourceGeneration = 17;
			input.analysisSourceGeneration = 17;
			input.frameSourceGeneration = 17;
			input.sourceFormatMatches = true;

			const ProfileTransitionRetentionDecision decision =
				EvaluateProfileTransitionRetention(input);
			Assert::IsTrue(decision.retainSourceGeometry);
			Assert::IsFalse(decision.retainSubtitleState);
			Assert::IsFalse(decision.retainNlsPresentationIntent);
		}

		TEST_METHOD(ProfileTransitionRejectsStaleOrUntrustedGeometry)
		{
			ProfileTransitionRetentionInput input;
			input.geometryAvailable = true;
			input.classification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.geometry = {
				0, 120, 3840, 2040, 3840, 2160, 2.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.geometrySourceGeneration = 17;
			input.analysisSourceGeneration = 17;
			input.frameSourceGeneration = 18;
			input.sourceFormatMatches = true;
			Assert::IsFalse(EvaluateProfileTransitionRetention(input).
				retainSourceGeometry);

			input.frameSourceGeneration = 17;
			// A profile which did not use active-picture geometry may keep this
			// trusted source-only snapshot dormant. The renderer force-verifies it
			// before any later profile can present it.
			Assert::IsTrue(EvaluateProfileTransitionRetention(input).
				retainSourceGeometry);

			input.classification = ActivePictureClassification::PROVISIONAL;
			Assert::IsFalse(EvaluateProfileTransitionRetention(input).
				retainSourceGeometry);

			input.classification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.sourceFormatMatches = false;
			Assert::IsFalse(EvaluateProfileTransitionRetention(input).
				retainSourceGeometry);
		}

		TEST_METHOD(BarContentDetectorNeverManufacturesAnOppositeEdge)
		{
			Assert::AreEqual(static_cast<int>(BarContentEdge::TOP),
				static_cast<int>(SelectVerticalBarContentEdge(170.0f, 0.0f)));
			Assert::AreEqual(static_cast<int>(BarContentEdge::BOTTOM),
				static_cast<int>(SelectVerticalBarContentEdge(0.0f, 116.0f)));

			// Bottom wins even when the top requires a larger reveal.
			Assert::AreEqual(static_cast<int>(BarContentEdge::BOTTOM),
				static_cast<int>(SelectVerticalBarContentEdge(170.0f, 116.0f)));
			Assert::AreEqual(static_cast<int>(BarContentEdge::BOTTOM),
				static_cast<int>(SelectVerticalBarContentEdge(42.0f, 116.0f)));
			Assert::AreEqual(static_cast<int>(BarContentEdge::NONE),
				static_cast<int>(SelectVerticalBarContentEdge(0.5f, 0.5f)));
		}

		TEST_METHOD(VerticalBarPolicyTranslatesOnlyOneOverlayLikeEdge)
		{
			VerticalBarContentInput input;
			input.lowerContent = true;
			input.lowerOccupiedDepth = 42;
			input.lowerPeakSamples = 220;
			input.lowerBarPixels = 280;
			input.upperBarPixels = 280;
			input.sampledColumns = 1800;
			input.lowerRequiredShift = 75.0f;
			auto decision = EvaluateVerticalBarContent(input);
			Assert::IsTrue(decision.lowerOverlayLike);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(75.0f, decision.translationPixels, 0.001f);

			input = {};
			input.upperContent = true;
			input.upperOccupiedDepth = 36;
			input.upperPeakSamples = 180;
			input.upperBarPixels = 280;
			input.lowerBarPixels = 280;
			input.sampledColumns = 1800;
			input.upperRequiredShift = 91.0f;
			decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(-91.0f, decision.translationPixels, 0.001f);
		}

		TEST_METHOD(VerticalBarPolicyTranslatesThinFullWidthVolumeButFitsPictureFill)
		{
			VerticalBarContentInput input;
			input.upperContent = true;
			input.upperOccupiedDepth = 42;
			input.upperPeakSamples = 1000; // thin, almost full-width volume OSD
			input.upperBarPixels = 280;
			input.lowerBarPixels = 280;
			input.sampledColumns = 1800;
			input.upperRequiredShift = 75.0f;
			auto decision = EvaluateVerticalBarContent(input);
			Assert::IsTrue(decision.upperOverlayLike);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(-75.0f, decision.translationPixels, 0.001f);

			input.upperOccupiedDepth = 150; // deep and full-width picture fill
			decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.action));
		}

		TEST_METHOD(VerticalBarPolicyAlwaysPrefersBottomOverlay)
		{
			VerticalBarContentInput input;
			input.upperContent = true;
			// Values reproduced from the Eternals trace: sparse top UI and a
			// deeper bottom subtitle were both correctly classified as overlays.
			input.upperOccupiedDepth = 70;
			input.upperPeakSamples = 133;
			input.upperBarPixels = 276;
			input.lowerBarPixels = 276;
			input.sampledColumns = 1800;
			input.upperRequiredShift = 153.0f;

			input.lowerContent = true;
			input.lowerOccupiedDepth = 90;
			input.lowerPeakSamples = 238;
			input.lowerRequiredShift = 199.0f;
			auto decision = EvaluateVerticalBarContent(input);
			Assert::IsTrue(decision.upperOverlayLike);
			Assert::IsTrue(decision.lowerOverlayLike);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(199.0f, decision.translationPixels, 0.001f);

			// A larger top intrusion must not reverse bottom priority.
			input.upperRequiredShift = 205.0f;
			decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(199.0f, decision.translationPixels, 0.001f);

			// Once the lower subtitle owns presentation, the top overlay cannot
			// reverse it even if the top would otherwise request a larger shift.
			input.bottomTranslationHeld = true;
			decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(199.0f, decision.translationPixels, 0.001f);
		}

		TEST_METHOD(VerticalBarPolicyBottomOverridesTopFirstHold)
		{
			VerticalBarContentInput input;
			input.upperContent = true;
			input.upperOccupiedDepth = 42;
			input.upperPeakSamples = 220;
			input.upperBarPixels = input.lowerBarPixels = 280;
			input.sampledColumns = 1800;
			input.upperRequiredShift = 170.0f;
			Assert::AreEqual(-170.0f,
				EvaluateVerticalBarContent(input).translationPixels, 0.001f);
			input.topTranslationHeld = true;
			input.lowerContent = true;
			input.lowerOccupiedDepth = 42;
			input.lowerPeakSamples = 220;
			input.lowerRequiredShift = 75.0f;
			for (bool upperStillPresent : {true, false})
			{
				input.upperContent = upperStillPresent;
				input.upperRequiredShift = upperStillPresent ? 170.0f : 0.0f;
				const auto decision = EvaluateVerticalBarContent(input);
				Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::TRANSLATE),
					static_cast<int>(decision.action));
				Assert::AreEqual(75.0f, decision.translationPixels, 0.001f);
			}
		}

		TEST_METHOD(VerticalBarPolicyFitsTwoEdgePictureEvidence)
		{
			VerticalBarContentInput input;
			input.upperContent = true;
			input.upperOccupiedDepth = 150;
			input.upperPeakSamples = 1500;
			input.upperBarPixels = 280;
			input.lowerContent = true;
			input.lowerOccupiedDepth = 160;
			input.lowerPeakSamples = 1500;
			input.lowerBarPixels = 280;
			input.sampledColumns = 1800;
			input.upperRequiredShift = 75.0f;
			input.lowerRequiredShift = 75.0f;
			auto decision = EvaluateVerticalBarContent(input);
			Assert::IsFalse(decision.upperOverlayLike);
			Assert::IsFalse(decision.lowerOverlayLike);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.action));

			// One picture-like edge is enough to distinguish a real fill from two
			// simultaneous sparse overlays.
			input.lowerOccupiedDepth = 42;
			input.lowerPeakSamples = 220;
			decision = EvaluateVerticalBarContent(input);
			Assert::IsTrue(decision.lowerOverlayLike);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.action));
		}

		TEST_METHOD(VerticalBarPolicyPreservesHeldTranslationAgainstOppositeOverlay)
		{
			VerticalBarContentInput input;
			input.upperContent = true;
			input.upperOccupiedDepth = 42;
			input.upperPeakSamples = 220;
			input.upperBarPixels = 280;
			input.lowerBarPixels = 280;
			input.sampledColumns = 1800;
			input.upperRequiredShift = 75.0f;

			input.bottomTranslationHeld = true;
			auto decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(decision.action));

			// Picture-like evidence on the opposite bar still has immediate Fit
			// authority; only a thin overlay is held.
			input.upperOccupiedDepth = 150;
			input.upperPeakSamples = 1500;
			decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.action));
		}

		TEST_METHOD(VerticalBarResolutionKeepsOneEdgeTranslationStable)
		{
			VerticalBarPresentationResolutionInput input;
			input.detailedAction = VerticalBarPresentationAction::TRANSLATE;
			input.translationPixels = 75.0f;
			input.authoritativeTop = 274;
			input.authoritativeBottom = 1884;
			input.rasterHeight = 2160;
			input.genericLowerExpansion = true; // same lower edge
			input.genericLowerBound = 1960;
			auto decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(76.0f, decision.translationPixels, 0.001f);

			input.genericLowerBound = 1988; // farther coarse same-edge envelope
			decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(76.0f, decision.translationPixels, 0.001f);

			// A generic top envelope accompanied this real lower subtitle in the
			// live trace. It cannot turn the dense one-edge subtitle into Fit.
			input.genericUpperExpansion = true;
			input.genericUpperBound = 54;
			decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(76.0f, decision.translationPixels, 0.001f);

			input = {};
			input.detailedAction = VerticalBarPresentationAction::TRANSLATE;
			input.translationPixels = -75.0f;
			input.authoritativeTop = 274;
			input.authoritativeBottom = 1884;
			input.rasterHeight = 2160;
			input.genericUpperExpansion = true;
			input.genericUpperBound = 198;
			decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			input.genericUpperBound = 100;
			decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(-76.0f, decision.translationPixels, 0.001f);

			input = {};
			input.genericUpperExpansion = true;
			decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(decision.action));
			input.genericLowerExpansion = true;
			input.genericVerticalFitConfirmed = true;
			input.genericVerticalFitAuthoritative = true;
			decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.action));
		}

        TEST_METHOD(ClassicRevealsBroadBarCardsWithoutGlyphOrOverlayShapeProof)
        {
            // Ben-Hur live trace: picture 384..1776; the bottom information
            // card occupies 1778..2158, peak 1693/1800 samples, depth 380.
            VerticalBarContentInput content;
            content.lowerContent = true;
            content.lowerOccupiedDepth = 380;
            content.lowerOccupiedRows = 358;
            content.lowerPeakSamples = 1693;
            content.lowerBarPixels = content.upperBarPixels = 384;
            content.sampledColumns = 1800;
            content.lowerRequiredShift = 384.0f; // padded extent clamps to raster
            const auto raw = EvaluateVerticalBarContent(content);
            Assert::IsFalse(raw.lowerOverlayLike);
            Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT), static_cast<int>(raw.action));
            Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT),
                static_cast<int>(SelectVerticalBarContentPresentation(raw, true, &content).action));
            Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT),
                static_cast<int>(SelectVerticalBarContentPresentation(raw, false, &content, true).action));
            for (bool topFirst : {false, true}) {
                content.upperContent = topFirst;
                content.upperOccupiedDepth = topFirst ? 380 : 0;
                content.upperPeakSamples = topFirst ? 1693 : 0;
                content.upperRequiredShift = topFirst ? 384.0f : 0.0f;
                content.topTranslationHeld = topFirst;
                auto decision = SelectVerticalBarContentPresentation(
                    EvaluateVerticalBarContent(content), true, &content, true);
                Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::TRANSLATE), static_cast<int>(decision.action));
                Assert::AreEqual(384.0f, decision.translationPixels, 0.001f);
                VerticalTranslationConfirmationState previous;
                for (uint64_t sequence = 1; sequence <= 3; ++sequence) {
                    VerticalTranslationConfirmationInput confirmation;
                    confirmation.previous = previous;
                    confirmation.observed = decision;
                    confirmation.sourceSequence = sequence;
                    confirmation.maximumTranslationMagnitudePixels = 384.0f;
                    auto result = ConfirmVerticalTranslation(confirmation);
                    previous = result.state;
                    Assert::AreEqual(sequence < 3, result.pending);
                    if (sequence == 3) {
                        auto crop = TrustedScopeCrop();
                        crop.geometry.top = 384; crop.geometry.bottom = 1776;
                        crop.geometry.aspectRatio = 3840.0 / 1392.0;
                        crop.verticalTranslationBase = crop.geometry;
                        crop.verticalTranslationActive = true;
                        crop.verticalTranslationPixels = static_cast<int>(result.effective.translationPixels);
                        crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
                        const auto presented = Evaluate(crop);
                        Assert::IsTrue(presented.applyCrop && presented.verticallyTranslated);
                        Assert::AreEqual(768, presented.sourceBounds.top);
                        Assert::AreEqual(2160, presented.sourceBounds.bottom);
                        Assert::AreEqual(1392, presented.sourceBounds.bottom - presented.sourceBounds.top);
                    }
                }
            }
            content.lowerContent = false; content.lowerRequiredShift = 0;
            auto top = SelectVerticalBarContentPresentation(EvaluateVerticalBarContent(content), true, &content, true);
            Assert::AreEqual(-384.0f, top.translationPixels, 0.001f);
            content.bottomTranslationHeld = true;
            Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::NONE), static_cast<int>(
                SelectVerticalBarContentPresentation(EvaluateVerticalBarContent(content), true, &content, true).action));
            content.upperPeakSamples = 0;
            Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FAIL_OPEN), static_cast<int>(
                SelectVerticalBarContentPresentation(EvaluateVerticalBarContent(content), true, &content, true).action));
        }

        TEST_METHOD(OffFitsChangingOverlayExtentsWithoutWaitingForClassicMotionTarget)
        {
            VerticalFitConfirmationState fitState;
            VerticalTranslationConfirmationState motionState;
            for(uint64_t seq=1;seq<=3;++seq) {
                VerticalBarContentDecision raw;
                raw.action=VerticalBarPresentationAction::TRANSLATE;
                raw.translationPixels=seq==1?70.0f:seq==2?140.0f:-90.0f;
                raw.upperOverlayLike=raw.lowerOverlayLike=true;
                VerticalTranslationConfirmationInput motion;
                motion.previous=motionState;motion.sourceSequence=seq;
                motion.observed=SelectVerticalBarContentPresentation(raw,false);
                const auto noMotion=ConfirmVerticalTranslation(motion);
                Assert::IsFalse(noMotion.pending);
                Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT),static_cast<int>(noMotion.effective.action));
                Assert::AreEqual(0.0f,noMotion.effective.translationPixels,0.001f);
                const auto fit=ConfirmVerticalFit(fitState,noMotion.effective,seq);
                fitState=fit.state;motionState=noMotion.state;
                Assert::AreEqual(seq==1,fit.pending);
                if(seq>1)Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT),static_cast<int>(fit.effective.action));
                Assert::IsTrue(noMotion.effective.upperOverlayLike && noMotion.effective.lowerOverlayLike);
            }
            // Off does not convert missing/failed analysis into a fit certificate.
            for(auto action:{VerticalBarPresentationAction::NONE,VerticalBarPresentationAction::FAIL_OPEN}) {
                VerticalBarContentDecision raw;raw.action=action;
                Assert::AreEqual(static_cast<int>(action),static_cast<int>(SelectVerticalBarContentPresentation(raw,false).action));
            }
        }

        TEST_METHOD(ClassicAndAdvancedModesKeepExistingTranslationConfirmation)
        {
            for(bool top:{false,true}) {
                VerticalTranslationConfirmationState previous;
                for(uint64_t seq=1;seq<=3;++seq) {
                    VerticalBarContentDecision raw;raw.action=VerticalBarPresentationAction::TRANSLATE;
                    raw.translationPixels=top?-90.0f:90.0f;raw.upperOverlayLike=top;raw.lowerOverlayLike=!top;
                    VerticalTranslationConfirmationInput motion;motion.previous=previous;motion.sourceSequence=seq;
                    motion.observed=SelectVerticalBarContentPresentation(raw,true);
                    const auto result=ConfirmVerticalTranslation(motion);previous=result.state;
                    Assert::AreEqual(seq<3,result.pending);
                    if(seq==3) {
                        Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::TRANSLATE),static_cast<int>(result.effective.action));
                        Assert::IsTrue(top?result.effective.translationPixels<0:result.effective.translationPixels>0);
                    }
                }
            }
        }

        TEST_METHOD(OffEnvelopeIncludesAllFourRenderedEdgesWithoutFullRasterFallback)
        {
            const ActivePictureBounds picture={400,380,3440,1780,3840,2160,3040.0/1400.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            PresentationEnvelopeGeometryInput input;input.trustedPicture=picture;
            input.observedContentAvailable=true;
            input.observedContent={300,330,3540,1960,3840,2160,0,ActivePictureBounds::BarAxes::NONE};
            input.expandLeft=input.expandTop=input.expandRight=input.expandBottom=true;
            input.horizontalPadding=input.verticalPadding=8;
            const auto envelope=BuildPresentationEnvelope(input);
            Assert::IsTrue(envelope.valid && envelope.expanded);
            Assert::AreEqual(292,envelope.bounds.left);Assert::AreEqual(322,envelope.bounds.top);
            Assert::AreEqual(3548,envelope.bounds.right);Assert::AreEqual(1968,envelope.bounds.bottom);
            Assert::IsTrue(envelope.bounds.left>0 && envelope.bounds.top>0 && envelope.bounds.right<3840 && envelope.bounds.bottom<2160);
            const auto screen=FitAspect(2.35,{0,0,3840,2160},VerticalPictureAlignment::CENTER);
            const auto displayed=FitAspect(double(envelope.bounds.right-envelope.bounds.left)/
                (envelope.bounds.bottom-envelope.bounds.top),screen.picture,VerticalPictureAlignment::CENTER);
            Assert::IsTrue(screen.valid && displayed.valid);
            Assert::IsTrue(displayed.picture.left>0 && displayed.picture.top>0 &&
                displayed.picture.right<3840 && displayed.picture.bottom<2160);
        }

		TEST_METHOD(DenseFitRequiresTwoConsecutiveAnalyzedSamples)
		{
			VerticalBarContentDecision fit;
			fit.action = VerticalBarPresentationAction::FIT;

			VerticalFitConfirmationState state;
			auto decision = ConfirmVerticalFit(state, fit);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(1U, decision.state.confirmations);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(decision.effective.action));

			state = decision.state;
			decision = ConfirmVerticalFit(state, fit);
			Assert::IsFalse(decision.pending);
			Assert::IsTrue(decision.newlyAccepted);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.effective.action));

			VerticalBarContentDecision overlay;
			overlay.action = VerticalBarPresentationAction::TRANSLATE;
			overlay.translationPixels = 100.0f;
			decision = ConfirmVerticalFit(decision.state, overlay);
			Assert::AreEqual(0U, decision.state.confirmations);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.effective.action));

			decision = ConfirmVerticalFit(decision.state, fit);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(decision.effective.action));
		}

		TEST_METHOD(CadenceRepeatCannotAdvanceDenseConfirmations)
		{
			VerticalBarContentDecision fit;
			fit.action = VerticalBarPresentationAction::FIT;
			auto fitDecision = ConfirmVerticalFit({}, fit, 50);
			Assert::AreEqual(1U, fitDecision.state.confirmations);
			fitDecision = ConfirmVerticalFit(fitDecision.state, fit, 50);
			Assert::IsTrue(fitDecision.pending);
			Assert::AreEqual(1U, fitDecision.state.confirmations);
			fitDecision = ConfirmVerticalFit(fitDecision.state, fit, 51);
			Assert::IsTrue(fitDecision.newlyAccepted);

			VerticalTranslationConfirmationInput translation;
			translation.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			translation.observed.translationPixels = 192.0f;
			translation.sourceSequence = 60;
			auto translationDecision =
				ConfirmVerticalTranslation(translation);
			Assert::AreEqual(1U,
				translationDecision.state.confirmations);

			translation.previous = translationDecision.state;
			translationDecision = ConfirmVerticalTranslation(translation);
			Assert::IsTrue(translationDecision.pending);
			Assert::AreEqual(1U,
				translationDecision.state.confirmations);

			translation.previous = translationDecision.state;
			translation.sourceSequence = 61;
			translationDecision = ConfirmVerticalTranslation(translation);
			Assert::AreEqual(2U,
				translationDecision.state.confirmations);

			translation.previous = translationDecision.state;
			translationDecision = ConfirmVerticalTranslation(translation);
			Assert::IsTrue(translationDecision.pending);
			Assert::AreEqual(2U,
				translationDecision.state.confirmations);

			translation.previous = translationDecision.state;
			translation.sourceSequence = 62;
			translationDecision = ConfirmVerticalTranslation(translation);
			Assert::IsTrue(translationDecision.newlyAccepted);
		}

		TEST_METHOD(DenseArbitrationSuppressesCoarseTwoEdgeFitUntilAccepted)
		{
			VerticalBarPresentationResolutionInput input;
			input.genericUpperExpansion = true;
			input.genericLowerExpansion = true;
			input.genericVerticalFitConfirmed = true;
			input.genericVerticalFitAuthoritative = true;
			input.denseVerticalArbitrationEnabled = true;
			input.authoritativeTop = 360;
			input.authoritativeBottom = 1800;
			input.rasterHeight = 2160;

			// Sequence 1496: the coarse envelope is not itself a Fit decision.
			auto decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(decision.action));
			const ActivePictureBounds trusted = {
				0, 360, 3840, 1800, 3840, 2160,
				3840.0 / 1440.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds expanded = {
				0, 148, 3840, 1960, 3840, 2160,
				3840.0 / 1812.0, ActivePictureBounds::BarAxes::NONE };
			Input crop = TrustedScopeCrop();
			crop.geometry = trusted;
			crop.outwardExpansion = expanded;
			crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
			auto routing = ResolveVerticalBarRendererRouting(decision);
			crop.outwardPresentationActive = routing.fitActive;
			crop.outwardExpansionAvailable = routing.fitActive;
			auto presented = Evaluate(crop);
			Assert::IsTrue(presented.applyCrop);
			Assert::IsFalse(presented.outwardExpanded);
			Assert::AreEqual(360, presented.sourceBounds.top);
			Assert::AreEqual(1800, presented.sourceBounds.bottom);

			// A Fit already accepted by the dense two-sample policy remains
			// authoritative and takes the existing bounded outward route.
			input.detailedAction = VerticalBarPresentationAction::FIT;
			decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.action));
			routing = ResolveVerticalBarRendererRouting(decision);
			crop.outwardPresentationActive = routing.fitActive;
			crop.outwardExpansionAvailable = routing.fitActive;
			presented = Evaluate(crop);
			Assert::IsTrue(presented.applyCrop);
			Assert::IsTrue(presented.outwardExpanded);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::OUTWARD_FIT),
				static_cast<int>(presented.owner));
			Assert::AreEqual(148, presented.sourceBounds.top);
			Assert::AreEqual(1960, presented.sourceBounds.bottom);
		}

		TEST_METHOD(DenseFitConfirmationDoesNotDelayFailOpen)
		{
			VerticalFitConfirmationState pending;
			pending.confirmations = 1;
			VerticalBarContentDecision unsafe;
			unsafe.action = VerticalBarPresentationAction::FAIL_OPEN;
			const auto decision = ConfirmVerticalFit(pending, unsafe);
			Assert::AreEqual(0U, decision.state.confirmations);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FAIL_OPEN),
				static_cast<int>(decision.effective.action));
		}

		TEST_METHOD(SubtitleHoldRejectsMixedFrameGenericAspectFit)
		{
			// The live trace accumulated a top volume envelope and lower subtitle
			// envelope at different times. A subtitle can disappear briefly before
			// its next cue, so retain its placement through the configured hold.
			VerticalBarPresentationState state;
			state.action = VerticalBarPresentationAction::TRANSLATE;
			state.translationPixels = 197.0f;
			state.detectedBottom = 2040;
			state.lastDetectionTick = 1000;
			state.sourceSequence = 101;
			Assert::IsTrue(IsVerticalBarPresentationActive(
				state, 1900, 2000, 102));
			VerticalBarPresentationUpdateInput refreshedSubtitle;
			refreshedSubtitle.previous = state;
			refreshedSubtitle.current.action =
				VerticalBarPresentationAction::TRANSLATE;
			// A small change still needs to snap outward: retaining the old 197 px
			// shift would cut off the newly lower subtitle.
			refreshedSubtitle.current.translationPixels = 205.0f;
			refreshedSubtitle.lowerContent = true;
			refreshedSubtitle.lowerContentBottom = 2068;
			refreshedSubtitle.currentTick = 1900;
			refreshedSubtitle.currentSourceSequence = 102;
			refreshedSubtitle.holdMs = 2000;
			refreshedSubtitle.translationEnabled = true;
			state = UpdateVerticalBarPresentation(refreshedSubtitle);
			Assert::AreEqual(205.0f, state.translationPixels, 0.001f);
			Assert::IsTrue(IsVerticalBarPresentationActive(
				state, 3800, 2000, 103));

			// A competing fit must not undo the active subtitle placement. It is
			// deliberately not a fresh subtitle detection, so it cannot extend the
			// release timer indefinitely.
			VerticalBarPresentationUpdateInput competingFit;
			competingFit.previous = state;
			competingFit.current.action = VerticalBarPresentationAction::FIT;
			competingFit.upperContent = true;
			competingFit.lowerContent = true;
			competingFit.currentTick = 2000;
			competingFit.currentSourceSequence = 103;
			competingFit.holdMs = 2000;
			competingFit.translationEnabled = true;
			state = UpdateVerticalBarPresentation(competingFit);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(state.action));
			Assert::AreEqual(205.0f, state.translationPixels, 0.001f);
			Assert::AreEqual(static_cast<unsigned long long>(1900),
				static_cast<unsigned long long>(state.lastDetectionTick));

			VerticalBarContentInput oppositeOverlay;
			oppositeOverlay.upperContent = true;
			oppositeOverlay.upperOccupiedDepth = 42;
			oppositeOverlay.upperPeakSamples = 220;
			oppositeOverlay.upperBarPixels = 280;
			oppositeOverlay.lowerBarPixels = 280;
			oppositeOverlay.sampledColumns = 1800;
			oppositeOverlay.upperRequiredShift = 91.0f;
			oppositeOverlay.bottomTranslationHeld = true;
			const auto overlayDecision =
				EvaluateVerticalBarContent(oppositeOverlay);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(overlayDecision.action));

			VerticalBarPresentationResolutionInput input;
			input.detailedAction = state.action;
			input.translationPixels = state.translationPixels;
			input.genericUpperExpansion = true;
			input.genericLowerExpansion = true;
			input.genericUpperBound = 140;
			input.genericLowerBound = 2054;
			input.authoritativeTop = 276;
			input.authoritativeBottom = 1884;
			input.rasterHeight = 2160;
			const auto heldAction = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(heldAction.action));
			Assert::AreEqual(206.0f, heldAction.translationPixels, 0.001f);

			// Once the dense release timer expires, a stale generic union still
			// cannot Fit. Only current simultaneous two-edge content can do that.
			input.detailedAction = VerticalBarPresentationAction::NONE;
			input.translationPixels = 0.0f;
			const auto staleAction = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(staleAction.action));
			input.genericVerticalFitConfirmed = true;
			const auto provisionalTwoEdgeAction =
				ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(provisionalTwoEdgeAction.action));
			input.genericVerticalFitAuthoritative = true;
			const auto currentTwoEdgeAction =
				ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(currentTwoEdgeAction.action));
		}

		TEST_METHOD(SubtitleTranslationDriftEasesInAndOutAndZeroSnaps)
		{
			VerticalTranslationDrift drift;
			Assert::AreEqual(0.0f, drift.Resolve(198.0f, 1000, 3000), 0.001f);
			Assert::AreEqual(99.0f, drift.Resolve(198.0f, 2500, 3000), 0.001f);
			Assert::AreEqual(198.0f, drift.Resolve(198.0f, 4000, 3000), 0.001f);
			Assert::AreEqual(198.0f,
				drift.Resolve(0.0f, 5000, 3000), 0.001f);
			Assert::AreEqual(99.0f,
				drift.Resolve(0.0f, 6500, 3000), 0.001f);
			Assert::AreEqual(226.0f,
				drift.Resolve(226.0f, 6600, 0), 0.001f);
			Assert::AreEqual(0.0f,
				drift.Resolve(0.0f, 6700, 0), 0.001f);
			Assert::IsFalse(drift.IsActive());
			Assert::IsTrue(drift.ConsumeFinalBaseFrame());
			Assert::IsFalse(drift.ConsumeFinalBaseFrame());
			Assert::AreEqual(0.0f,
				drift.Resolve(0.0f, 7100, 0), 0.001f);
			Assert::IsFalse(drift.IsActive());
		}

		TEST_METHOD(NewVerticalTranslationRequiresThreeStableAnalysisSamples)
		{
			VerticalTranslationConfirmationInput input;
			input.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.observed.translationPixels = 192.0f;

			auto decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);
			Assert::IsFalse(decision.newlyAccepted);
			Assert::AreEqual(1u, decision.state.confirmations);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(decision.effective.action));

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(2u, decision.state.confirmations);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsFalse(decision.pending);
			Assert::IsTrue(decision.newlyAccepted);
			Assert::AreEqual(0u, decision.state.confirmations);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.effective.action));
			Assert::AreEqual(192.0f,
				decision.effective.translationPixels, 0.001f);
		}

		TEST_METHOD(ChangingVerticalTranslationTargetRestartsConfirmation)
		{
			VerticalTranslationConfirmationInput input;
			input.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.observed.translationPixels = 192.0f;
			auto decision = ConfirmVerticalTranslation(input);

			input.previous = decision.state;
			input.observed.translationPixels = 210.0f;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(1u, decision.state.confirmations);
			Assert::AreEqual(210.0f,
				decision.state.candidateTranslationPixels, 0.001f);

			input.previous = decision.state;
			input.observed.translationPixels = 209.0f;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(2u, decision.state.confirmations);

			input.previous = decision.state;
			input.observed.translationPixels = 210.0f;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.newlyAccepted);
			// Stable estimates within two pixels choose the farther reveal.
			Assert::AreEqual(210.0f,
				decision.effective.translationPixels, 0.001f);
		}

		TEST_METHOD(LargerTranslationConfirmsWithoutMovingAcceptedTargetEarly)
		{
			VerticalTranslationConfirmationInput input;
			input.acceptedTranslationActive = true;
			input.acceptedTranslationPixels = 192.0f;
			input.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.observed.translationPixels = 210.0f;

			auto decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(192.0f,
				decision.effective.translationPixels, 0.001f);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(192.0f,
				decision.effective.translationPixels, 0.001f);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.newlyAccepted);
			Assert::AreEqual(210.0f,
				decision.effective.translationPixels, 0.001f);

			input.previous = {};
			input.acceptedTranslationPixels = 210.0f;
			input.observed.translationPixels = 208.0f;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsFalse(decision.pending);
			Assert::IsFalse(decision.newlyAccepted);
		}

		TEST_METHOD(TargetBufferOvershootsAndAbsorbsLaterGrowth)
		{
			VerticalTranslationConfirmationInput input;
			input.targetBufferPixels = 10.0f;
			input.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.observed.translationPixels = 206.0f;

			auto decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.newlyAccepted);
			Assert::AreEqual(216.0f,
				decision.effective.translationPixels, 0.001f);

			input.previous = {};
			input.acceptedTranslationActive = true;
			input.acceptedTranslationPixels = 216.0f;
			input.observed.translationPixels = 210.0f;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsFalse(decision.pending);
			Assert::IsFalse(decision.newlyAccepted);
			Assert::AreEqual(216.0f,
				decision.effective.translationPixels, 0.001f);
		}

		TEST_METHOD(TargetBufferMirrorsForUpperEdgeAndStillAllowsLargeGrowth)
		{
			VerticalTranslationConfirmationInput input;
			input.targetBufferPixels = 10.0f;
			input.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.observed.translationPixels = -206.0f;

			auto decision = ConfirmVerticalTranslation(input);
			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.newlyAccepted);
			Assert::AreEqual(-216.0f,
				decision.effective.translationPixels, 0.001f);

			input.previous = {};
			input.acceptedTranslationActive = true;
			input.acceptedTranslationPixels = -216.0f;
			input.observed.translationPixels = -228.0f;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);
			Assert::AreEqual(-216.0f,
				decision.effective.translationPixels, 0.001f);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.newlyAccepted);
			Assert::AreEqual(-238.0f,
				decision.effective.translationPixels, 0.001f);
		}

		TEST_METHOD(TargetBufferCannotTranslatePastTheSourceRaster)
		{
			VerticalTranslationConfirmationInput input;
			input.targetBufferPixels = 10.0f;
			input.maximumTranslationMagnitudePixels = 276.0f;
			input.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.observed.translationPixels = 272.0f;

			auto decision = ConfirmVerticalTranslation(input);
			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);

			input.previous = decision.state;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.newlyAccepted);
			Assert::AreEqual(276.0f,
				decision.effective.translationPixels, 0.001f);
		}

		TEST_METHOD(NonTranslationObservationCancelsPendingConfirmation)
		{
			VerticalTranslationConfirmationInput input;
			input.observed.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.observed.translationPixels = -170.0f;
			auto decision = ConfirmVerticalTranslation(input);
			Assert::IsTrue(decision.pending);

			input.previous = decision.state;
			input.observed = {};
			decision = ConfirmVerticalTranslation(input);
			Assert::IsFalse(decision.pending);
			Assert::AreEqual(0u, decision.state.confirmations);

			input.previous.candidateTranslationPixels = -170.0f;
			input.previous.confirmations = 1;
			input.observed.action = VerticalBarPresentationAction::FIT;
			decision = ConfirmVerticalTranslation(input);
			Assert::IsFalse(decision.pending);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(decision.effective.action));
		}

		TEST_METHOD(OnlyProvisionalVerticalEdgesStartInspectionRetention)
		{
			Assert::IsTrue(ShouldRetainTrustedBaseForVerticalInspection(
				true, true, true, false, false, false, true));
			Assert::IsTrue(ShouldRetainTrustedBaseForVerticalInspection(
				true, true, true, false, true, false, false));
			Assert::IsFalse(ShouldRetainTrustedBaseForVerticalInspection(
				false, true, true, false, false, false, true));
			Assert::IsFalse(ShouldRetainTrustedBaseForVerticalInspection(
				true, false, true, false, false, false, true));
			Assert::IsFalse(ShouldRetainTrustedBaseForVerticalInspection(
				true, true, false, false, false, false, true));
			Assert::IsFalse(ShouldRetainTrustedBaseForVerticalInspection(
				true, true, true, true, false, false, true));
			Assert::IsTrue(ShouldRetainTrustedBaseForVerticalInspection(
				true, true, true, false, true, false, true));
			Assert::IsFalse(ShouldRetainTrustedBaseForVerticalInspection(
				true, true, true, true, true, false, true));
			Assert::IsFalse(ShouldRetainTrustedBaseForVerticalInspection(
				true, true, true, false, true, true, false));
		}

		TEST_METHOD(FixedCropTracksMenusOnlyWhenDynamicGeometryIsEnabled)
		{
			Assert::IsFalse(ShouldTrackDynamicPresentationGeometry(
				false, false));
			Assert::IsTrue(ShouldTrackDynamicPresentationGeometry(
				true, false));
			Assert::IsTrue(ShouldTrackDynamicPresentationGeometry(
				false, true));
			Assert::IsTrue(ShouldTrackDynamicPresentationGeometry(
				true, true));

			Input menu = TrustedScopeCrop();
			menu.geometry = {
				0, 120, 3840, 2116, 3840, 2160,
				3840.0 / 1996.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			menu.automaticCropEnabled =
				ShouldTrackDynamicPresentationGeometry(false, false);
			const Decision anchoredSource = Evaluate(menu);
			AssertFullRaster(anchoredSource);

			FixedAspectCropInput fixed;
			fixed.fixedAspect = 2.4;
			fixed.sourceBounds = anchoredSource.sourceBounds;
			const AspectLimitFillDecision anchored =
				EvaluateFixedAspectCrop(fixed);
			Assert::AreEqual(280, anchored.sourceBounds.top);
			Assert::AreEqual(1880, anchored.sourceBounds.bottom);

			menu.automaticCropEnabled =
				ShouldTrackDynamicPresentationGeometry(false, true);
			const Decision subtitleSource = Evaluate(menu);
			Assert::IsTrue(subtitleSource.applyCrop);
			fixed.sourceBounds = subtitleSource.sourceBounds;
			const AspectLimitFillDecision subtitleAdjusted =
				EvaluateFixedAspectCrop(fixed);
			Assert::AreEqual(318, subtitleAdjusted.sourceBounds.top);
			Assert::AreEqual(1918, subtitleAdjusted.sourceBounds.bottom);
		}

		TEST_METHOD(VerticalInspectionBridgeStopsAfterDenseClassification)
		{
			VerticalInspectionBridgeInput input;
			input.candidate = true;
			input.sourceGeneration = 7;
			input.presentationEpoch = 11;
			input.trustedBase = TrustedScopeCrop().geometry;
			input.sourceSequence = 100;

			auto decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.started);
			Assert::IsFalse(decision.retain);
			Assert::IsFalse(decision.expired);
			Assert::IsFalse(decision.state.retentionConsumed);

			input.previous = decision.state;
			input.sourceSequence = 101;
			input.denseAnalysisCompleted = true;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsFalse(decision.retain);
			Assert::IsFalse(decision.expired);
			Assert::IsTrue(decision.state.denseAnalysisCompleted);

			input.previous = decision.state;
			input.sourceSequence = 102;
			input.denseAnalysisCompleted = false;
			input.retentionRequested = true;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsFalse(decision.retain);
			Assert::IsTrue(decision.expired);
			Assert::IsFalse(decision.state.retentionConsumed);
		}

		TEST_METHOD(VerticalInspectionBridgeRepeatIsIdempotentAndDropoutDoesNotRearm)
		{
			VerticalInspectionBridgeInput input;
			input.candidate = true;
			input.retentionRequested = true;
			input.sourceGeneration = 7;
			input.presentationEpoch = 11;
			input.trustedBase = TrustedScopeCrop().geometry;
			input.sourceSequence = 100;

			auto decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.retain);
			Assert::IsFalse(decision.expired);
			Assert::AreEqual(100ull,
				decision.state.retainedSourceSequence);

			// Cadence repeats render the same decoded sequence again. The already
			// chosen presentation must be stable for every presentation of it.
			input.previous = decision.state;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.retain);
			Assert::IsFalse(decision.expired);

			// Losing the coarse envelope for one dark/noisy frame preserves the
			// spent episode lockout instead of silently rearming it.
			input.previous = decision.state;
			input.candidate = false;
			input.retentionRequested = false;
			input.sourceSequence = 101;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.state.active);
			Assert::IsTrue(decision.state.retentionConsumed);

			input.previous = decision.state;
			input.candidate = true;
			input.retentionRequested = true;
			input.sourceSequence = 102;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.retain);
			Assert::IsFalse(decision.expired);

			// The bounded handoff is not a rearm: the next fresh source sample
			// fails open when dense analysis has still not published a result.
			input.previous = decision.state;
			input.sourceSequence = 103;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsFalse(decision.retain);
			Assert::IsTrue(decision.expired);
			Assert::IsTrue(decision.state.failOpenLatched);
		}

		TEST_METHOD(ConfirmedCurrentVerticalFitResolvesSpentInspectionBridge)
		{
			VerticalInspectionBridgeInput input;
			input.candidate = true;
			input.retentionRequested = true;
			input.sourceGeneration = 7;
			input.presentationEpoch = 11;
			input.trustedBase = TrustedScopeCrop().geometry;
			input.sourceSequence = 100;

			auto decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.retain);

			// A later unresolved sample spends the single retained source sequence
			// and latches fail-open.
			input.previous = decision.state;
			input.denseAnalysisCompleted = true;
			input.sourceSequence = 101;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.expired);
			Assert::IsTrue(decision.state.failOpenLatched);

			// A mere candidate cannot clear the spent episode.
			input.previous = decision.state;
			input.retentionRequested = false;
			input.denseAnalysisCompleted = false;
			input.sourceSequence = 102;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.state.failOpenLatched);

			// Only the renderer's verified, current dense-Fit result may close it.
			input.previous = decision.state;
			input.confirmedVerticalFitResolved = true;
			input.sourceSequence = 103;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsFalse(decision.state.active);
			Assert::IsFalse(decision.state.failOpenLatched);

			// The next subtitle episode can now use its bounded inspection handoff.
			input.previous = decision.state;
			input.confirmedVerticalFitResolved = false;
			input.retentionRequested = true;
			input.sourceSequence = 104;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.started);
			Assert::IsTrue(decision.retain);
		}

		TEST_METHOD(OnlyCurrentFullWidthVerticalDenseFitMayResolveInspection)
		{
			VerticalInspectionFitResolutionInput input;
			input.confirmedDenseFit = true;
			input.denseAnalysisCurrent = true;
			input.outwardExpansionAvailable = true;
			input.trustedBase = TrustedScopeCrop().geometry;
			input.outwardExpansion = input.trustedBase;
			input.outwardExpansion.bottom = 2116;
			input.outwardExpansion.aspectRatio = 3840.0 / (2116 - 274);
			input.outwardExpansion.trustedBarAxes =
				ActivePictureBounds::BarAxes::NONE;
			input.outwardExpansionSourceGeneration = 7;
			input.frameSourceGeneration = 7;
			Assert::IsTrue(CanResolveVerticalInspectionWithConfirmedFit(input));

			input.confirmedDenseFit = false;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.confirmedDenseFit = true;
			input.denseAnalysisCurrent = false;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.denseAnalysisCurrent = true;
			input.outwardExpansionAvailable = false;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.outwardExpansionAvailable = true;
			input.currentHorizontalExpansion = true;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.currentHorizontalExpansion = false;
			input.outwardExpansion.left = 2;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.outwardExpansion.left = 0;
			input.outwardExpansion.right = 3838;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.outwardExpansion.right = 3840;
			input.outwardExpansion.bottom = 2115;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.outwardExpansion.bottom = 2116;
			input.outwardExpansionSourceGeneration = 6;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
			input.outwardExpansionSourceGeneration = 7;
			input.outwardExpansion = input.trustedBase;
			Assert::IsFalse(CanResolveVerticalInspectionWithConfirmedFit(input));
		}

		TEST_METHOD(VerticalInspectionBridgeRearmsOnlyForResolvedOrNewProvenance)
		{
			VerticalInspectionBridgeInput input;
			input.candidate = true;
			input.retentionRequested = true;
			input.sourceGeneration = 7;
			input.presentationEpoch = 11;
			input.trustedBase = TrustedScopeCrop().geometry;
			input.sourceSequence = 100;

			auto decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.retain);

			input.previous = decision.state;
			input.fullRasterAuthorityResolved = true;
			input.candidate = false;
			input.retentionRequested = false;
			input.sourceSequence = 101;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsFalse(decision.state.active);

			input.previous = decision.state;
			input.fullRasterAuthorityResolved = false;
			input.candidate = true;
			input.retentionRequested = true;
			input.sourceSequence = 102;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.started);
			Assert::IsTrue(decision.retain);

			input.previous = decision.state;
			input.presentationEpoch = 12;
			input.sourceSequence = 103;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.started);
			Assert::IsTrue(decision.retain);

			input.previous = decision.state;
			input.trustedBase.top += 2;
			input.sourceSequence = 104;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.started);
			Assert::IsTrue(decision.retain);

			input.previous = decision.state;
			input.sourceGeneration = 8;
			input.sourceSequence = 200;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.started);
			Assert::IsTrue(decision.retain);
		}

		TEST_METHOD(ResolvedCropRearmsLaterSubtitleWithoutFullRasterFlash)
		{
			VerticalInspectionBridgeInput bridge;
			bridge.candidate = true;
			bridge.retentionRequested = true;
			bridge.sourceGeneration = 7;
			bridge.presentationEpoch = 11;
			bridge.trustedBase = TrustedScopeCrop().geometry;
			bridge.sourceSequence = 421;

			auto decision = UpdateVerticalInspectionBridge(bridge);
			Assert::IsTrue(decision.retain);

			// Dense analysis spends this unresolved episode. It must stay spent
			// through candidate dropouts, but not through later trusted authority.
			bridge.previous = decision.state;
			bridge.sourceSequence = 422;
			bridge.denseAnalysisCompleted = true;
			decision = UpdateVerticalInspectionBridge(bridge);
			Assert::IsFalse(decision.retain);
			Assert::IsTrue(decision.expired);
			Assert::IsTrue(decision.state.denseAnalysisCompleted);
			Assert::IsTrue(decision.state.failOpenLatched);

			bridge.previous = decision.state;
			bridge.candidate = false;
			bridge.retentionRequested = false;
			bridge.denseAnalysisCompleted = false;
			bridge.cropAuthorityResolved = true;
			bridge.sourceSequence = 500;
			decision = UpdateVerticalInspectionBridge(bridge);
			Assert::IsFalse(decision.state.active);
			Assert::IsFalse(decision.state.retentionConsumed);
			Assert::IsFalse(decision.state.denseAnalysisCompleted);
			Assert::IsFalse(decision.state.failOpenLatched);

			// A later subtitle is a new unresolved episode even though it uses the
			// same source, profile epoch, and trusted base as the earlier subtitle.
			bridge.previous = decision.state;
			bridge.candidate = true;
			bridge.retentionRequested = true;
			bridge.cropAuthorityResolved = false;
			bridge.sourceSequence = 8782;
			decision = UpdateVerticalInspectionBridge(bridge);
			Assert::IsTrue(decision.started);
			Assert::IsTrue(decision.retain);
			Assert::IsFalse(decision.expired);

			Input pending = TrustedScopeCrop();
			pending.latestObservationSupportsCrop = false;
			pending.latestObservationIsProvisional = true;
			pending.latestObservationClassification =
				ActivePictureClassification::PROVISIONAL;
			pending.frameLocalPresentationRetentionEvaluated = true;
			pending.frameLocalPresentationRetentionSafe = false;
			pending.verticalInspectionPending = decision.retain;
			pending.verticalInspectionSourceGeneration =
				bridge.sourceGeneration;
			pending.verticalInspectionSourceSequence = bridge.sourceSequence;
			pending.frameSourceSequence = bridge.sourceSequence;
			const Decision retained = Evaluate(pending);
			Assert::IsTrue(retained.applyCrop);
			Assert::IsFalse(retained.outwardExpanded);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::VERTICAL_INSPECTION),
				static_cast<int>(retained.owner));
			Assert::AreEqual(0, retained.sourceBounds.left);
			Assert::AreEqual(274, retained.sourceBounds.top);
			Assert::AreEqual(3840, retained.sourceBounds.right);
			Assert::AreEqual(1884, retained.sourceBounds.bottom);

			// Once confirmed, the existing subtitle policy translates the same-size
			// window. Rearming the bridge must not change aspect ratio or NLS input.
			Input translated = pending;
			translated.verticalInspectionPending = false;
			translated.verticalTranslationActive = true;
			translated.verticalTranslationPixels = 212;
			translated.verticalTranslationBase = translated.geometry;
			translated.verticalTranslationSourceGeneration = 7;
			const Decision presented = Evaluate(translated);
			Assert::IsTrue(presented.applyCrop);
			Assert::IsFalse(presented.outwardExpanded);
			Assert::IsTrue(presented.verticallyTranslated);
			Assert::AreEqual(212, presented.verticalTranslationPixels);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::VERTICAL_TRANSLATION),
				static_cast<int>(presented.owner));
			Assert::AreEqual(0, presented.sourceBounds.left);
			Assert::AreEqual(486, presented.sourceBounds.top);
			Assert::AreEqual(3840, presented.sourceBounds.right);
			Assert::AreEqual(2096, presented.sourceBounds.bottom);
			Assert::AreEqual(
				retained.sourceBounds.right - retained.sourceBounds.left,
				presented.sourceBounds.right - presented.sourceBounds.left);
			Assert::AreEqual(
				retained.sourceBounds.bottom - retained.sourceBounds.top,
				presented.sourceBounds.bottom - presented.sourceBounds.top);

			// Rearming restores only the bounded analysis handoff; it does not
			// create an open-ended hold when authority remains unresolved.
			bridge.previous = decision.state;
			bridge.sourceSequence = 8783;
			decision = UpdateVerticalInspectionBridge(bridge);
			Assert::IsTrue(decision.retain);
			Assert::IsFalse(decision.expired);

			bridge.previous = decision.state;
			bridge.sourceSequence = 8784;
			decision = UpdateVerticalInspectionBridge(bridge);
			Assert::IsTrue(decision.retain);
			Assert::IsFalse(decision.expired);

			bridge.previous = decision.state;
			bridge.sourceSequence = 8785;
			decision = UpdateVerticalInspectionBridge(bridge);
			Assert::IsFalse(decision.retain);
			Assert::IsTrue(decision.expired);
			Assert::IsTrue(decision.state.failOpenLatched);
		}

		TEST_METHOD(DenseVerticalOwnerClosesInspectionBeforeFailOpen)
		{
			VerticalInspectionBridgeInput input;
			input.candidate = true;
			input.retentionRequested = true;
			input.sourceGeneration = 7;
			input.presentationEpoch = 11;
			input.trustedBase = TrustedScopeCrop().geometry;
			input.sourceSequence = 100;

			auto decision = UpdateVerticalInspectionBridge(input);
			Assert::IsTrue(decision.retain);

			// Dense analysis has identified a vertical overlay and transferred the
			// following confirmation samples to the translation/Fit policy.
			input.previous = decision.state;
			input.denseAnalysisCompleted = true;
			input.verticalPresentationOwnerAvailable = true;
			input.sourceSequence = 101;
			decision = UpdateVerticalInspectionBridge(input);
			Assert::IsFalse(decision.state.active);
			Assert::IsFalse(decision.state.failOpenLatched);
			Assert::IsFalse(decision.retain);
			Assert::IsFalse(decision.expired);
		}

		TEST_METHOD(PendingTranslationRetainsTrustedCropWithoutFullRasterFlash)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.latestObservationClassification =
				ActivePictureClassification::PROVISIONAL;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			input.verticalTranslationConfirmationPending = true;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration = 7;

			const Decision retained = Evaluate(input);
			Assert::IsTrue(retained.applyCrop);
			Assert::AreEqual(274, retained.sourceBounds.top);
			Assert::AreEqual(1884, retained.sourceBounds.bottom);
			Assert::IsTrue(retained.reason.find("confirms") !=
				std::string::npos);

			Input stale = input;
			stale.verticalTranslationSourceGeneration = 6;
			AssertFullRaster(Evaluate(stale));

			Input nonContainingBar = input;
			nonContainingBar.latestObservationIsProvisional = false;
			nonContainingBar.latestObservationClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			const Decision retainedAcrossBarRefinement =
				Evaluate(nonContainingBar);
			Assert::IsTrue(retainedAcrossBarRefinement.applyCrop);
			Assert::AreEqual(274,
				retainedAcrossBarRefinement.sourceBounds.top);
			Assert::AreEqual(1884,
				retainedAcrossBarRefinement.sourceBounds.bottom);

			Input picture = input;
			picture.latestObservationIsProvisional = false;
			picture.latestObservationClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			AssertFullRaster(Evaluate(picture));
		}

		TEST_METHOD(DarkSceneCutInspectionRetainsTrustedCropBeforeDenseBaseExists)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.latestObservationClassification =
				ActivePictureClassification::PROVISIONAL;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			input.sceneVerificationHoldActive = true;
			input.verticalInspectionPending = true;
			input.verticalInspectionSourceGeneration = 7;
			input.verticalInspectionSourceSequence = 5257;
			input.frameSourceSequence = 5257;

			// A dark star-field scene cut can make the first overlay/envelope sample
			// provisional and frame-local pixel-unsafe. The scene hold cannot retain
			// that frame, and no dense translation base or evidence generation has
			// been established yet.
			Assert::AreEqual(0ull, input.verticalTranslationSourceGeneration);

			const Decision retained = Evaluate(input);
			Assert::IsTrue(retained.applyCrop);
			Assert::IsFalse(retained.outwardExpanded);
			Assert::IsFalse(retained.verticallyTranslated);
			Assert::AreEqual(274, retained.sourceBounds.top);
			Assert::AreEqual(1884, retained.sourceBounds.bottom);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::VERTICAL_INSPECTION),
				static_cast<int>(retained.owner));
			Assert::IsTrue(retained.reason.find("inspection") !=
				std::string::npos);

			Input noInspection = input;
			noInspection.verticalInspectionPending = false;
			const Decision withdrawn = Evaluate(noInspection);
			AssertFullRaster(withdrawn);
			Assert::AreEqual(static_cast<int>(
				WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED),
				static_cast<int>(withdrawn.withdrawalCause));

			Input trustedFullRaster = input;
			trustedFullRaster.fullRasterPresentationAuthoritative = true;
			AssertFullRaster(Evaluate(trustedFullRaster));

			Input observedFullRaster = input;
			observedFullRaster.latestObservationClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			AssertFullRaster(Evaluate(observedFullRaster));

			Input staleGeometry = input;
			staleGeometry.geometrySourceGeneration = 6;
			AssertFullRaster(Evaluate(staleGeometry));

			Input staleInspection = input;
			staleInspection.verticalInspectionSourceGeneration = 6;
			AssertFullRaster(Evaluate(staleInspection));

			Input nextProvisionalFrame = input;
			nextProvisionalFrame.frameSourceSequence = 5258;
			AssertFullRaster(Evaluate(nextProvisionalFrame));

			Input explicitFailOpen = input;
			explicitFailOpen.presentationFailOpen = true;
			AssertFullRaster(Evaluate(explicitFailOpen));
		}

		TEST_METHOD(PendingDenseFitRetainsTrustedCropWithoutFullRasterFlash)
		{
			Input input = TrustedScopeCrop();
			input.geometry = {
				0, 360, 3840, 1800, 3840, 2160,
				3840.0 / 1440.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.latestObservationClassification =
				ActivePictureClassification::PROVISIONAL;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			input.verticalFitConfirmationPending = true;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration = 7;

			// Sequence 1526 logged Fit pending 1/2, so final crop evaluation must
			// agree with that decision instead of returning full raster.
			const Decision retained = Evaluate(input);
			Assert::IsTrue(retained.applyCrop);
			Assert::IsFalse(retained.outwardExpanded);
			Assert::IsFalse(retained.verticallyTranslated);
			Assert::AreEqual(360, retained.sourceBounds.top);
			Assert::AreEqual(1800, retained.sourceBounds.bottom);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::FIT_CONFIRMATION),
				static_cast<int>(retained.owner));
			Assert::IsTrue(retained.reason.find("fit") != std::string::npos);

			Input stale = input;
			stale.verticalTranslationSourceGeneration = 6;
			AssertFullRaster(Evaluate(stale));

			Input fullRaster = input;
			fullRaster.fullRasterPresentationAuthoritative = true;
			AssertFullRaster(Evaluate(fullRaster));
		}

		TEST_METHOD(ScrollingTextRetentionCannotIntroduceUnpresentedCrop)
		{
			Input crop = TrustedScopeCrop();
			crop.geometry = { 0, 476, 3840, 1688, 3840, 2160,
				3840.0 / 1212.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.frameLocalPresentationRetentionEvaluated = true;
			crop.frameLocalPresentationRetentionSafe = false;
			crop.barCropRefinementPending = true;
			crop.verticalTranslationConfirmationPending = true;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration = 7;
			CropPresentationAdmissionState admission;
			for (uint64_t sequence = 984; sequence < 994; ++sequence)
			{
				crop.frameSourceSequence = sequence;
				// Both owners admitted the unpresented 3.17:1 text envelope.
				const auto candidate = Evaluate(crop);
				Assert::IsTrue(candidate.applyCrop);
				const auto actual = AdmitCropPresentation(admission, crop, candidate, 0);
				AssertFullRaster(actual.presentation);
				Assert::IsTrue(actual.blocked);
				admission = actual.state;
				Assert::IsFalse(admission.available);
			}
			// The following title episode remains full raster. Fresh picture proof
			// can then acquire ordinary scope without a new timer or restart.
			crop.nearBlackEpisodeFullRaster = true;
			AssertFullRaster(AdmitCropPresentation(admission, crop, Evaluate(crop), 0).presentation);
			crop = TrustedScopeCrop();
			const auto scope = AdmitCropPresentation(admission, crop, Evaluate(crop), 0);
			Assert::IsTrue(scope.presentation.applyCrop);
			Assert::IsTrue(scope.state.available);
			Assert::AreEqual(274, scope.presentation.sourceBounds.top);
		}

		TEST_METHOD(PendingPresentationOwnersCannotBootstrapAnUnseenCrop)
		{
			for (int owner = 0; owner < 12; ++owner)
			{
				Input crop = TrustedScopeCrop();
				crop.latestObservationSupportsCrop = false;
				crop.latestObservationIsProvisional = true;
				crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
				crop.frameSourceSequence = 100;
				crop.verticalTranslationBase = crop.geometry;
				crop.verticalTranslationSourceGeneration = 7;
				if (owner == 0) { crop.latestObservationIsProvisional = false;
					crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
					crop.barCropRefinementPending = true; }
				if (owner == 1) crop.verticalTranslationConfirmationPending = true;
				if (owner == 2) crop.verticalFitConfirmationPending = true;
				if (owner == 3) { crop.verticalInspectionPending = true;
					crop.verticalInspectionSourceGeneration = 7; crop.verticalInspectionSourceSequence = 100; }
				if (owner == 4) { crop.frameLocalPresentationRetentionEvaluated = true;
					crop.frameLocalPresentationRetentionSafe = true; }
				if (owner == 5) crop.nearBlackEpisodeRetainCrop = true;
				if (owner == 6) crop.sceneVerificationHoldActive = true;
				if (owner == 7) crop.ambiguityHoldActive = true;
				if (owner == 8) { crop.outwardPresentationActive = crop.outwardExpansionAvailable = true;
					crop.outwardExpansion = crop.geometry; crop.outwardExpansion.bottom += 20;
					crop.outwardExpansionSourceGeneration = 7; }
				if (owner == 9) { crop.verticalTranslationActive = true; crop.verticalTranslationPixels = 20; }
				if (owner == 10) crop.verticalTranslationBaseRetentionActive = true;
				if (owner == 11) crop.verticalTranslationEngageBaseRetentionActive = true;
				const auto candidate = Evaluate(crop);
				Assert::IsTrue(candidate.applyCrop);
				AssertFullRaster(AdmitCropPresentation({}, crop, candidate, 3).presentation);
				// The identical pending owner may retain a picture already acquired.
				// Establish the reference through actual clean picture acquisition,
				// not by relabeling the same pending retention owner as supported.
				Input acquisition = TrustedScopeCrop();
				acquisition.geometry = crop.geometry;
				acquisition.geometrySourceGeneration = crop.geometrySourceGeneration;
				acquisition.frameSourceGeneration = crop.frameSourceGeneration;
				acquisition.frameSourceSequence = crop.frameSourceSequence;
				const auto prior = AdmitCropPresentation({}, acquisition, Evaluate(acquisition), 3).state;
				Assert::IsTrue(AdmitCropPresentation(prior, crop, candidate, 3).presentation.applyCrop);
			}
		}

		TEST_METHOD(ZdfNewCropCannotAcquireThroughPendingOwnerDespiteSupportingObservation)
		{
			for (const auto bounds : { ActivePictureBounds{0,196,3840,1964,3840,2160,
				3840.0/1768.0,ActivePictureBounds::BarAxes::TOP_BOTTOM},
				ActivePictureBounds{0,168,3840,1988,3840,2160,
				3840.0/1820.0,ActivePictureBounds::BarAxes::TOP_BOTTOM} })
			for (bool fitPending : { false, true })
			{
				auto crop = TrustedScopeCrop();
				crop.geometry = bounds;
				crop.frameSourceSequence = 5071;
				crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.frameLocalPresentationRetentionEvaluated = true;
				crop.frameLocalPresentationRetentionSafe = false;
				crop.verticalTranslationConfirmationPending = !fitPending;
				crop.verticalFitConfirmationPending = fitPending;
				crop.verticalTranslationBase = crop.geometry;
				crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
				// Logical publication sets supports=true, but current excluded-band
				// inspection has already contradicted the newly proposed crop.
				Assert::IsTrue(crop.latestObservationSupportsCrop);
				const auto candidate = Evaluate(crop);
				Assert::IsTrue(candidate.applyCrop);
				Assert::AreEqual(int(fitPending ? DecisionOwner::FIT_CONFIRMATION :
					DecisionOwner::TRANSLATION_CONFIRMATION), int(candidate.owner));
				const auto shown = AdmitCropPresentation({}, crop, candidate, 3);
				Assert::IsTrue(shown.blocked,
					L"A pending presentation correction cannot acquire an unseen crop even when logical observation supports it.");
				AssertFullRaster(shown.presentation);
				Assert::IsFalse(shown.state.available);
			}
		}

		TEST_METHOD(ZdfAcquisitionGuardPreservesAlreadyPresentedCaptionBase)
		{
			for (bool fitPending : { false, true })
			{
				auto crop = TrustedScopeCrop();
				crop.frameSourceSequence = 5000;
				crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.frameLocalPresentationRetentionEvaluated = true;
				crop.frameLocalPresentationRetentionSafe = true;
				const auto clean = Evaluate(crop);
				Assert::AreEqual(int(DecisionOwner::TRUSTED_CROP),int(clean.owner));
				const auto prior = AdmitCropPresentation({},crop,clean,3);
				Assert::IsTrue(prior.state.available);
				for (bool supports : { false, true })
				{
					++crop.frameSourceSequence;
					crop.latestObservationSupportsCrop = supports;
					crop.frameLocalPresentationRetentionSafe = false;
					crop.verticalTranslationConfirmationPending = !fitPending;
					crop.verticalFitConfirmationPending = fitPending;
					crop.verticalTranslationBase = crop.geometry;
					crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
					const auto shown = AdmitCropPresentation(prior.state,crop,Evaluate(crop),3);
					Assert::IsFalse(shown.blocked);
					Assert::IsTrue(shown.presentation.applyCrop);
					Assert::AreEqual(crop.geometry.top,shown.presentation.sourceBounds.top);
					Assert::AreEqual(crop.geometry.bottom,shown.presentation.sourceBounds.bottom);
				}
			}
		}

		TEST_METHOD(ZdfPendingToConfirmedFitCannotLaunderUnresolvedCropAcquisition)
		{
			auto crop = TrustedScopeCrop();
			crop.geometry = {0,196,3840,1964,3840,2160,3840.0/1768.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM};
			crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			crop.frameLocalPresentationRetentionEvaluated = true;
			crop.frameLocalPresentationRetentionSafe = false;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
			crop.currentVisibleBoundsAvailable = true;
			crop.currentVisibleBase = crop.geometry;
			crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
			crop.currentVisibleBounds = {0,0,3840,2160,3840,2160,16.0/9.0,
				ActivePictureBounds::BarAxes::NONE};
			CropPresentationAdmissionState admission;
			for (uint64_t sequence = 5071; sequence <= 5075; ++sequence)
			{
				crop.frameSourceSequence = crop.currentVisibleSourceSequence = sequence;
				crop.verticalTranslationConfirmationPending = sequence < 5073;
				crop.verticalFitConfirmationPending = sequence == 5073;
				crop.outwardPresentationActive = crop.outwardExpansionAvailable = sequence >= 5074;
				crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
				crop.outwardExpansion = {0,172,3840,2008,3840,2160,3840.0/1836.0,
					ActivePictureBounds::BarAxes::NONE};
				const auto candidate = Evaluate(crop);
				Assert::IsTrue(candidate.applyCrop);
				if (sequence >= 5074)
				{
					Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),int(candidate.owner));
					Assert::IsTrue(candidate.sourceBounds.top > crop.currentVisibleBounds.top);
				}
				// A changed owner name does not prove the still-excluded pixels safe.
				const auto shown = AdmitCropPresentation(admission,crop,candidate,3);
				AssertFullRaster(shown.presentation);
				Assert::IsTrue(shown.blocked);
				Assert::IsFalse(shown.state.available);
				admission = shown.state;
			}
			// Real clean bars can acquire immediately, without a timer or restart.
			++crop.frameSourceSequence;
			crop.outwardPresentationActive = crop.outwardExpansionAvailable = false;
			crop.currentVisibleBoundsAvailable = false;
			crop.frameLocalPresentationRetentionSafe = true;
			const auto acquired = AdmitCropPresentation(admission,crop,Evaluate(crop),3);
			Assert::IsFalse(acquired.blocked);
			Assert::IsTrue(acquired.presentation.applyCrop);
			Assert::IsTrue(acquired.state.available);
			Assert::AreEqual(int(DecisionOwner::TRUSTED_CROP),int(acquired.presentation.owner));
		}


        TEST_METHOD(ZdfUnseenConfirmedFitMustContainFreshVisiblePixelWitness)
        {
            auto crop = TrustedScopeCrop();
            crop.frameSourceSequence = 5074;
            crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            crop.outwardPresentationActive = crop.outwardExpansionAvailable = true;
            crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
            crop.outwardExpansion = crop.geometry;
            crop.outwardExpansion.top = 172;
            crop.outwardExpansion.bottom = 2008;
            crop.currentVisibleBoundsAvailable = true;
            crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
            crop.currentVisibleSourceSequence = crop.frameSourceSequence;
            crop.currentVisibleBase = crop.geometry;
            crop.currentVisibleBounds = crop.outwardExpansion;
            crop.currentVisibleBounds.top = 86;
            crop.currentVisibleBounds.bottom = 2080;
            crop.currentVisibleBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
            const auto candidate = Evaluate(crop);
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),int(candidate.owner));
            const auto shown = AdmitCropPresentation({},crop,candidate,3);
            Assert::IsTrue(shown.blocked);
            AssertFullRaster(shown.presentation);
            Assert::IsFalse(shown.state.available);
        }

        TEST_METHOD(ZdfAcquisitionWitnessRequiresCurrentValidMatchingBounds)
        {
            for (int fault = 0; fault < 8; ++fault)
            {
                auto crop = TrustedScopeCrop();
                crop.frameSourceSequence = 5074;
                crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.frameLocalPresentationRetentionEvaluated = true;
                crop.frameLocalPresentationRetentionSafe = false;
                crop.currentVisibleBoundsAvailable = true;
                crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
                crop.currentVisibleSourceSequence = crop.frameSourceSequence;
                crop.currentVisibleBase = crop.geometry;
                crop.currentVisibleBounds = {0,86,3840,2080,3840,2160,3840.0/1994.0,
                    ActivePictureBounds::BarAxes::NONE};
                if (fault == 0) crop.currentVisibleSourceGeneration--;
                if (fault == 1) crop.currentVisibleSourceSequence--;
                if (fault == 2) crop.currentVisibleBase.top += 8;
                if (fault == 3) crop.currentVisibleBounds.rasterWidth = 1920;
                if (fault == 4) crop.currentVisibleBounds.top = -2;
                if (fault == 5) crop.currentVisibleBounds.bottom = crop.currentVisibleBounds.top;
                if (fault == 6) crop.currentVisibleBoundsAvailable = false;
                if (fault == 7) crop.currentVisibleBase.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
                const auto candidate = Evaluate(crop);
                Assert::AreEqual(int(DecisionOwner::TRUSTED_CROP),int(candidate.owner));
                const auto shown = AdmitCropPresentation({},crop,candidate,3);
                Assert::IsFalse(shown.blocked,L"Stale or invalid evidence cannot veto unrelated current acquisition authority.");
                Assert::IsTrue(shown.presentation.applyCrop);
            }
        }

        TEST_METHOD(ZdfResolvedFitContainingCurrentPixelsCanAcquireWithoutPriorCrop)
        {
            auto crop = TrustedScopeCrop();
            crop.frameSourceSequence = 5074;
            crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = false;
            crop.outwardPresentationActive = crop.outwardExpansionAvailable = true;
            crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
            crop.outwardExpansion = crop.geometry;
            crop.outwardExpansion.top = 86;
            crop.outwardExpansion.bottom = 2080;
            crop.currentVisibleBoundsAvailable = true;
            crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
            crop.currentVisibleSourceSequence = crop.frameSourceSequence;
            crop.currentVisibleBase = crop.geometry;
            crop.currentVisibleBounds = crop.outwardExpansion;
            crop.currentVisibleBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
            const auto candidate = Evaluate(crop);
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),int(candidate.owner));
            const auto shown = AdmitCropPresentation({},crop,candidate,3);
            Assert::IsFalse(shown.blocked);
            Assert::IsTrue(shown.presentation.applyCrop);
            Assert::IsTrue(shown.state.available);
            Assert::AreEqual(86,shown.presentation.sourceBounds.top);
            Assert::AreEqual(2080,shown.presentation.sourceBounds.bottom);
        }

		TEST_METHOD(PresentationAdmissionPreservesRealAspectChangesAndExistingRetention)
		{
			CropPresentationAdmissionState state;
			// One unchanged raster, with real inward and outward format changes:
			// approximately 2.20 -> 1.43 -> 2.20 -> 2.35 -> 1.90 -> 2.35.
			for (int top : { 478, 8, 478, 532, 340, 532 })
			{
				Input crop = TrustedScopeCrop();
				crop.rasterHeight = 2700;
				crop.geometry = { 0, top, 3840, 2700-top, 3840, 2700,
					3840.0 / (2700-2*top), ActivePictureBounds::BarAxes::TOP_BOTTOM };
				const auto acquired = AdmitCropPresentation(state, crop, Evaluate(crop), 3);
				Assert::IsTrue(acquired.presentation.applyCrop);
				Assert::IsFalse(acquired.blocked);
				state = acquired.state;
				crop.latestObservationSupportsCrop = false;
				crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.barCropRefinementPending = true;
				const auto retained = AdmitCropPresentation(state, crop, Evaluate(crop), 3);
				Assert::IsTrue(retained.presentation.applyCrop);
				Assert::AreEqual(crop.geometry.top, retained.presentation.sourceBounds.top);
			}
		}

		TEST_METHOD(PresentationRetentionRejectsDifferentContractOrContext)
		{
			Input original = TrustedScopeCrop();
			const auto prior = AdmitCropPresentation({}, original, Evaluate(original), 3).state;
			for (int variant = 0; variant < 5; ++variant)
			{
				auto crop = original;
				crop.latestObservationSupportsCrop = false;
				crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
				crop.barCropRefinementPending = true;
				uint64_t epoch = 3;
				if (variant == 0) { crop.geometry.top = 476; crop.geometry.bottom = 1688; }
				if (variant == 1) crop.frameSourceGeneration = crop.geometrySourceGeneration = 8;
				if (variant == 2) epoch = 4;
				if (variant == 3) { crop.rasterWidth = crop.geometry.rasterWidth = crop.geometry.right = 1920; }
				if (variant == 4) crop.geometry.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
				AssertFullRaster(AdmitCropPresentation(prior, crop, Evaluate(crop), epoch).presentation,
					crop.rasterWidth, crop.rasterHeight);
			}
		}

		TEST_METHOD(TemporaryWithdrawalPreservesReferenceButFullRasterAuthorityClearsIt)
		{
			Input crop = TrustedScopeCrop();
			auto state = AdmitCropPresentation({}, crop, Evaluate(crop), 3).state;
			crop.presentationFailOpen = true;
			auto withdrawal = AdmitCropPresentation(state, crop, Evaluate(crop), 3);
			AssertFullRaster(withdrawal.presentation);
			Assert::IsTrue(withdrawal.state.available);
			crop.presentationFailOpen = false;
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsProvisional = true;
			crop.frameLocalPresentationRetentionSafe = true;
			Assert::IsTrue(AdmitCropPresentation(withdrawal.state, crop, Evaluate(crop), 3).presentation.applyCrop);
			crop.fullRasterPresentationAuthoritative = true;
			const auto full = AdmitCropPresentation(state, crop, Evaluate(crop), 3);
			Assert::IsFalse(full.state.available);
			crop.fullRasterPresentationAuthoritative = false;
			AssertFullRaster(AdmitCropPresentation(full.state, crop, Evaluate(crop), 3).presentation);
		}

		TEST_METHOD(BarCropRefinementRetainsTrustedCropUntilTransitionPublishes)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			input.barCropRefinementPending = true;

			const Decision retained = Evaluate(input);
			Assert::IsTrue(retained.applyCrop);
			Assert::AreEqual(274, retained.sourceBounds.top);
			Assert::AreEqual(1884, retained.sourceBounds.bottom);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::BAR_REFINEMENT),
				static_cast<int>(retained.owner));
			Assert::IsTrue(retained.reason.find("bar refinement") !=
				std::string::npos);

			Input composed = input;
			composed.outwardPresentationActive = true;
			composed.outwardExpansionAvailable = true;
			composed.outwardExpansion = composed.geometry;
			composed.outwardExpansion.bottom = 2000;
			composed.outwardExpansionSourceGeneration = 7;
			const Decision composedDecision = Evaluate(composed);
			Assert::IsTrue(composedDecision.outwardExpanded);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::BAR_REFINEMENT),
				static_cast<int>(composedDecision.owner));

			Input unbounded = input;
			unbounded.barCropRefinementPending = false;
			AssertFullRaster(Evaluate(unbounded));

			Input horizontalConflict = input;
			horizontalConflict.barCropRefinementHorizontalConflict = true;
			AssertFullRaster(Evaluate(horizontalConflict));

			Input fullRaster = input;
			fullRaster.fullRasterPresentationAuthoritative = true;
			AssertFullRaster(Evaluate(fullRaster));

			Input stale = input;
			stale.geometrySourceGeneration = 6;
			AssertFullRaster(Evaluate(stale));
		}

		TEST_METHOD(HorizontalConflictOverridesEveryProvisionalCropOwner)
		{
			Input base = TrustedScopeCrop();
			base.latestObservationSupportsCrop = false;
			base.latestObservationIsProvisional = true;
			base.latestObservationClassification =
				ActivePictureClassification::PROVISIONAL;
			base.frameLocalPresentationRetentionEvaluated = true;
			base.frameLocalPresentationRetentionSafe = false;

			Input translation = base;
			translation.verticalTranslationConfirmationPending = true;
			translation.verticalTranslationBase = translation.geometry;
			translation.verticalTranslationSourceGeneration = 7;
			Assert::IsTrue(Evaluate(translation).applyCrop);
			translation.barCropRefinementHorizontalConflict = true;
			AssertFullRaster(Evaluate(translation));

			Input fit = base;
			fit.verticalFitConfirmationPending = true;
			fit.verticalTranslationBase = fit.geometry;
			fit.verticalTranslationSourceGeneration = 7;
			Assert::IsTrue(Evaluate(fit).applyCrop);
			fit.barCropRefinementHorizontalConflict = true;
			AssertFullRaster(Evaluate(fit));

			Input scene = base;
			scene.frameLocalPresentationRetentionEvaluated = false;
			scene.sceneVerificationHoldActive = true;
			Assert::IsTrue(Evaluate(scene).applyCrop);
			scene.barCropRefinementHorizontalConflict = true;
			AssertFullRaster(Evaluate(scene));

			Input outward = base;
			outward.geometry = {
				480, 0, 3360, 2160, 3840, 2160, 4.0 / 3.0,
				ActivePictureBounds::BarAxes::LEFT_RIGHT };
			outward.outwardPresentationActive = true;
			outward.outwardExpansionAvailable = true;
			outward.outwardExpansion = outward.geometry;
			outward.outwardExpansion.left = 400;
			outward.outwardExpansionSourceGeneration = 7;
			Assert::IsTrue(Evaluate(outward).applyCrop);
			outward.barCropRefinementHorizontalConflict = true;
			AssertFullRaster(Evaluate(outward));
		}

		TEST_METHOD(ZeroDurationEngageStillAllowsTimedRelease)
		{
			VerticalTranslationDrift drift;
			Assert::AreEqual(192.0f,
				drift.Resolve(192.0f, 1000, 0), 0.001f);
			Assert::IsFalse(drift.IsActive());

			Assert::AreEqual(192.0f,
				drift.Resolve(0.0f, 2000, 2000), 0.001f);
			Assert::IsTrue(drift.IsActive());
			Assert::AreEqual(96.0f,
				drift.Resolve(0.0f, 3000, 2000), 0.001f);
			Assert::AreEqual(0.0f,
				drift.Resolve(0.0f, 4000, 2000), 0.001f);
			Assert::IsFalse(drift.IsActive());
			Assert::IsTrue(drift.ConsumeFinalBaseFrame());
		}

		TEST_METHOD(ZeroHoldRetainsPresentationUntilNextAnalysisSample)
		{
			VerticalBarPresentationState state;
			state.action = VerticalBarPresentationAction::TRANSLATE;
			state.translationPixels = 192.0f;
			state.lastDetectionTick = 1000;
			state.sourceSequence = 100;

			Assert::IsTrue(IsVerticalBarPresentationActiveForFrame(
				state, 1040, 0, 101, false, true, 7, 7));
			Assert::IsFalse(IsVerticalBarPresentationActiveForFrame(
				state, 1080, 0, 102, true, true, 7, 7));
			Assert::IsFalse(IsVerticalBarPresentationActiveForFrame(
				state, 1040, 0, 101, false, false, 7, 7));
			Assert::IsFalse(IsVerticalBarPresentationActiveForFrame(
				state, 1040, 0, 101, false, true, 7, 8));
		}

		TEST_METHOD(ZeroHoldTranslationOwnsCompetingFitUntilAnalyzedNegative)
		{
			VerticalBarPresentationUpdateInput input;
			input.previous.action = VerticalBarPresentationAction::TRANSLATE;
			input.previous.translationPixels = 192.0f;
			input.previous.detectedBottom = 2076;
			input.previous.lastDetectionTick = 1000;
			input.previous.sourceSequence = 100;
			input.current.action = VerticalBarPresentationAction::FIT;
			input.upperContent = true;
			input.lowerContent = true;
			input.upperContentTop = 68;
			input.lowerContentBottom = 2092;
			input.currentTick = 1040;
			input.currentSourceSequence = 103;
			input.holdMs = 0;
			input.translationEnabled = true;
			input.previousOwnsCurrentAnalysis = true;

			auto state = UpdateVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(state.action));
			Assert::AreEqual(192.0f, state.translationPixels, 0.001f);

			// Only a scheduled negative observation releases a zero-hold action.
			input.previous = state;
			input.current = {};
			input.upperContent = false;
			input.lowerContent = false;
			input.currentTick = 1080;
			input.currentSourceSequence = 106;
			state = UpdateVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(state.action));
		}

		TEST_METHOD(SubtitleOwnedTwoEdgePixelsCannotPublishNovelAspect)
		{
			const ActivePictureBounds trusted = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds overlayCandidate = {
				0, 68, 3840, 2092, 3840, 2160, 3840.0 / 2024.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			VerticalBarPresentationState presentation;
			presentation.action = VerticalBarPresentationAction::TRANSLATE;
			presentation.translationPixels = 198.0f;

			Assert::IsTrue(ShouldDeferVerticalGeometryTransition(
				trusted, overlayCandidate,
				ActivePictureClassification::BAR_CROP_TRUSTED,
				presentation, false, 7, 7));
			presentation = {};
			Assert::IsTrue(ShouldDeferVerticalGeometryTransition(
				trusted, overlayCandidate,
				ActivePictureClassification::BAR_CROP_TRUSTED,
				presentation, true, 7, 7));
			Assert::IsFalse(ShouldDeferVerticalGeometryTransition(
				trusted, overlayCandidate,
				ActivePictureClassification::BAR_CROP_TRUSTED,
				presentation, false, 7, 7));
			Assert::IsFalse(ShouldDeferVerticalGeometryTransition(
				trusted, overlayCandidate,
				ActivePictureClassification::BAR_CROP_TRUSTED,
				presentation, true, 7, 8));
		}

		TEST_METHOD(ZeroHoldTimedEngageCadenceNeverAlternatesFullRaster)
		{
			VerticalBarPresentationState state;
			VerticalTranslationDrift drift;
			for (uint64_t frame = 0; frame < 80; ++frame)
			{
				const uint64_t sequence = 100 + frame;
				const uint64_t tick = 1000 + frame * 40;
				const bool analyzed = frame % 3 == 0;
				if (analyzed)
				{
					VerticalBarPresentationUpdateInput update;
					update.previous = state;
					update.currentTick = tick;
					update.currentSourceSequence = sequence;
					update.holdMs = 0;
					update.translationEnabled = true;
					update.previousOwnsCurrentAnalysis =
						state.action != VerticalBarPresentationAction::NONE;
					if (frame < 12)
					{
						update.current.action =
							VerticalBarPresentationAction::TRANSLATE;
						update.current.translationPixels = 192.0f;
						update.lowerContent = true;
						update.lowerContentBottom = 2076;
					}
					state = UpdateVerticalBarPresentation(update);
				}

				const bool active = IsVerticalBarPresentationActiveForFrame(
					state, tick, 0, sequence, analyzed, true, 7, 7);
				const bool requested = active && state.action ==
					VerticalBarPresentationAction::TRANSLATE;
				const float resolved = drift.Resolve(requested ? 192.0f : 0.0f,
					tick, requested ? 500 : 2000);

				VerticalBarPresentationResolutionInput resolution;
				resolution.detailedAction = requested || std::abs(resolved) > 0.5f
					? VerticalBarPresentationAction::TRANSLATE
					: VerticalBarPresentationAction::NONE;
				resolution.translationPixels = resolved;
				resolution.zeroTranslationRetainsTrustedBase =
					requested && drift.IsActive();
				resolution.authoritativeTop = 274;
				resolution.authoritativeBottom = 1884;
				resolution.rasterHeight = 2160;
				const auto routing = ResolveVerticalBarRendererRouting(
					ResolveVerticalBarPresentation(resolution));
				Assert::IsFalse(routing.failOpen);
				Assert::IsFalse(routing.fitActive);

				Input crop = TrustedScopeCrop();
				crop.verticalTranslationActive = routing.translationActive;
				crop.verticalTranslationPixels = routing.translationPixels;
				crop.verticalTranslationBase = crop.geometry;
				crop.verticalTranslationSourceGeneration = 7;
				const Decision selected = Evaluate(crop);
				Assert::IsTrue(selected.applyCrop);
				Assert::AreEqual(1610, selected.sourceBounds.bottom -
					selected.sourceBounds.top);
			}
		}

		TEST_METHOD(ZeroShiftTimedEngageKeepsTrustedBaseInsteadOfFailingOpen)
		{
			VerticalBarPresentationResolutionInput resolutionInput;
			resolutionInput.detailedAction =
				VerticalBarPresentationAction::TRANSLATE;
			resolutionInput.translationPixels = 0.0f;
			resolutionInput.zeroTranslationRetainsTrustedBase = true;
			resolutionInput.authoritativeTop = 276;
			resolutionInput.authoritativeBottom = 1884;
			resolutionInput.rasterHeight = 2160;
			const auto resolution = ResolveVerticalBarPresentation(
				resolutionInput);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::NONE),
				static_cast<int>(resolution.action));

			Input crop = TrustedScopeCrop();
			const auto routing = ResolveVerticalBarRendererRouting(resolution);
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsProvisional = true;
			crop.latestObservationClassification =
				ActivePictureClassification::PROVISIONAL;
			crop.frameLocalPresentationRetentionEvaluated = true;
			crop.frameLocalPresentationRetentionSafe = false;
			crop.presentationFailOpen = routing.failOpen;
			crop.verticalTranslationActive = routing.translationActive;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration =
				crop.frameSourceGeneration;
			crop.verticalTranslationEngageBaseRetentionActive = true;
			const Decision selected = Evaluate(crop);
			Assert::IsTrue(selected.applyCrop);
			Assert::IsFalse(selected.verticallyTranslated);
			Assert::AreEqual(274, selected.sourceBounds.top);
			Assert::AreEqual(1884, selected.sourceBounds.bottom);
			Assert::IsTrue(selected.reason.find("engage origin") !=
				std::string::npos);

			crop.verticalTranslationEngageBaseRetentionActive = false;
			AssertFullRaster(Evaluate(crop));

			resolutionInput.zeroTranslationRetainsTrustedBase = false;
			const auto invalidZero = ResolveVerticalBarPresentation(
				resolutionInput);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FAIL_OPEN),
				static_cast<int>(invalidZero.action));
		}

		TEST_METHOD(RecordedBottomSubtitleKeepsConfirmedDenseMotionTarget)
		{
			// VP-0080 live trace: dense analysis found one lower subtitle and
			// requested +197 px, while the coarse envelope reported 54..2106.
			// The dense request already contains padding. Neither coarse edge may
			// retarget it or turn it into a scale-changing Fit.
			VerticalBarPresentationResolutionInput input;
			input.detailedAction = VerticalBarPresentationAction::TRANSLATE;
			input.translationPixels = 197.0f;
			input.genericUpperExpansion = true;
			input.genericUpperBound = 54;
			input.genericLowerExpansion = true;
			input.genericLowerBound = 2106;
			input.authoritativeTop = 276;
			input.authoritativeBottom = 1884;
			input.rasterHeight = 2160;

			const auto decision = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(198.0f, decision.translationPixels, 0.001f);
			const auto routing = ResolveVerticalBarRendererRouting(decision);
			Assert::IsTrue(routing.translationActive);
			Assert::IsFalse(routing.fitActive);
			Assert::IsFalse(routing.failOpen);
			Assert::AreEqual(198, routing.translationPixels);

			Input crop = TrustedScopeCrop();
			crop.geometry.top = input.authoritativeTop;
			crop.geometry.bottom = input.authoritativeBottom;
			crop.verticalTranslationActive = routing.translationActive;
			crop.verticalTranslationPixels = routing.translationPixels;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
			const Decision presented = Evaluate(crop);
			Assert::IsTrue(presented.applyCrop);
			Assert::IsTrue(presented.verticallyTranslated);
			Assert::IsFalse(presented.outwardExpanded);
			Assert::AreEqual(474, presented.sourceBounds.top);
			Assert::AreEqual(2082, presented.sourceBounds.bottom);
			Assert::AreEqual(1608,
				presented.sourceBounds.bottom - presented.sourceBounds.top);
		}

		TEST_METHOD(VerticalBarPresentationHoldsTranslationAgainstCompetingFit)
		{
			VerticalBarContentInput volume;
			volume.upperBarPixels = 280;
			volume.lowerContent = true;
			volume.lowerOccupiedDepth = 42;
			volume.lowerPeakSamples = 1000;
			volume.lowerBarPixels = 280;
			volume.sampledColumns = 1800;
			volume.lowerRequiredShift = 75.0f;
			VerticalBarPresentationUpdateInput update;
			update.current = EvaluateVerticalBarContent(volume);
			update.lowerContent = true;
			update.lowerContentBottom = 1988;
			update.currentTick = 1000;
			update.currentSourceSequence = 10;
			update.holdMs = 2000;
			update.translationEnabled = true;
			auto state = UpdateVerticalBarPresentation(update);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(state.action));
			Assert::AreEqual(75.0f, state.translationPixels, 0.001f);
			Assert::AreEqual(1988, state.detectedBottom);

			// A competing Fit during the active subtitle hold cannot change picture
			// geometry. It is not a subtitle refresh, so it does not extend the hold.
			update.previous = state;
			update.current = {};
			update.current.action = VerticalBarPresentationAction::FIT;
			update.upperContent = true;
			update.lowerContent = false;
			update.upperContentTop = 104;
			update.currentTick = 1100;
			update.currentSourceSequence = 11;
			state = UpdateVerticalBarPresentation(update);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(state.action));
			Assert::AreEqual(75.0f, state.translationPixels, 0.001f);
			Assert::AreEqual(0, state.detectedTop);
			Assert::AreEqual(1988, state.detectedBottom);
			Assert::IsTrue(IsVerticalBarPresentationActive(
				state, 3000, 2000, 12));
			Assert::IsFalse(IsVerticalBarPresentationActive(
				state, 3101, 2000, 12));
		}

		TEST_METHOD(VerticalBarCurrentFrameSurvivesZeroHoldAndThenReleases)
		{
			VerticalBarPresentationUpdateInput update;
			update.current.action = VerticalBarPresentationAction::TRANSLATE;
			update.current.translationPixels = -75.0f;
			update.upperContent = true;
			update.upperContentTop = 104;
			update.currentTick = 1000;
			update.currentSourceSequence = 42;
			update.holdMs = 0;
			update.translationEnabled = true;
			const auto state = UpdateVerticalBarPresentation(update);
			Assert::IsTrue(IsVerticalBarPresentationActive(
				state, 1001, 0, 42));
			Assert::IsFalse(IsVerticalBarPresentationActive(
				state, 1001, 0, 43));
		}

		TEST_METHOD(VerticalBarTranslationSurvivesSameGenerationAuthorityGap)
		{
			// The live Alpha path may classify a subtitle-bearing frame as
			// provisional while validating the bar geometry. That temporary gap
			// must not discard a current same-generation subtitle translation and
			// hand authority to the coarse two-edge envelope.
			VerticalBarPresentationState state;
			state.action = VerticalBarPresentationAction::TRANSLATE;
			state.translationPixels = 197.0f;
			state.detectedBottom = 2040;
			state.lastDetectionTick = 1000;
			state.sourceSequence = 101;

			Assert::IsTrue(CanRetainVerticalBarPresentationAcrossAuthorityGap(
				state, 7, 7, 1100, 2000, 102));
			Assert::IsFalse(CanRetainVerticalBarPresentationAcrossAuthorityGap(
				state, 7, 8, 1100, 2000, 102));
			Assert::IsFalse(CanRetainVerticalBarPresentationAcrossAuthorityGap(
				state, 7, 7, 3001, 2000, 102));

			VerticalBarPresentationResolutionInput input;
			input.detailedAction = state.action;
			input.translationPixels = state.translationPixels;
			input.genericUpperExpansion = true;
			input.genericUpperBound = 54;
			input.genericLowerExpansion = true;
			input.genericLowerBound = 2106;
			input.authoritativeTop = 276;
			input.authoritativeBottom = 1884;
			input.rasterHeight = 2160;
			const auto action = ResolveVerticalBarPresentation(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(action.action));
			Assert::AreEqual(198.0f, action.translationPixels, 0.001f);
		}

        TEST_METHOD(ExpiredTranslationWithRecentConfirmedFitCanRequestFreshDenseInspection)
        {
            VerticalBarContentDecision fitDecision;
            fitDecision.action=VerticalBarPresentationAction::FIT;
            const auto first=ConfirmVerticalFit({},fitDecision,98);
            const auto confirmed=ConfirmVerticalFit(first.state,fitDecision,99);
            Assert::IsTrue(first.pending);
            Assert::IsTrue(confirmed.newlyAccepted);
            for (uint64_t age=1;age<=3;++age)
            {
                auto input=ExpiredTranslationWithTwoEdgeEnvelope();
                input.currentSourceSequence=99+age;
                Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input),
                    L"The old subtitle hold has expired; ordinary held analysis no longer inspects it.");
                Assert::IsTrue(CanInspectRetiringTranslationFit(input,confirmed.state,false),
                    L"Recent confirmed FIT and a current two-edge envelope must permit a fresh dense scan at translation expiry.");
                Assert::IsTrue(input.latestClassification==ActivePictureClassification::PROVISIONAL);
                Assert::IsFalse(input.currentBarAuthority,L"Inspection eligibility must not manufacture crop authority.");
            }
        }

        TEST_METHOD(RetiringTranslationInspectionRejectsActiveStaleOrIncompatibleHistory)
        {
            for (int fault=0;fault<32;++fault)
            {
                auto input=ExpiredTranslationWithTwoEdgeEnvelope();
                VerticalFitConfirmationState fit{2,99};
                bool blocked=false;
                switch (fault)
                {
                case 0: input.currentTick=2999; break;
                case 1: input.currentTick=3000; break; // Existing hold includes the exact endpoint.
                case 2: input.holdMs=0; break;
                case 3: input.currentTick=999; break;
                case 4: input.presentation.lastDetectionTick=0; break;
                case 5: input.presentation.sourceSequence=0; break;
                case 6: input.presentation.sourceSequence=input.currentSourceSequence; break;
                case 7: input.presentation.sourceSequence=input.currentSourceSequence+1; break;
                case 8: input.currentSourceSequence=0; break;
                case 9: fit.confirmations=1; break;
                case 10: fit.lastObservedSourceSequence=0; break;
                case 11: fit.lastObservedSourceSequence=100; break;
                case 12: fit.lastObservedSourceSequence=101; break;
                case 13: fit.lastObservedSourceSequence=96; break;
                case 14: input.trustedBarGeometryAvailable=false; break;
                case 15: input.storedBaseMatchesTrustedGeometry=false; break;
                case 16: input.currentEnvelopeAvailable=false; break;
                case 17: input.currentBarAuthority=true; break;
                case 18: input.evidenceSourceGeneration=0; break;
                case 19: ++input.currentSourceGeneration; break;
                case 20: input.latestClassification=ActivePictureClassification::BAR_CROP_TRUSTED; break;
                case 21: input.latestClassification=ActivePictureClassification::UNAVAILABLE; break;
                case 22: input.currentEnvelope.top=input.trustedGeometry.top; break;
                case 23: input.currentEnvelope.bottom=input.trustedGeometry.bottom; break;
                case 24: input.currentEnvelope.left=4; break;
                case 25: input.presentation.action=VerticalBarPresentationAction::FIT; break;
                case 26: blocked=true; break;
                case 27: input.trustedGeometry.left=4; input.currentEnvelope.left=4; break;
                case 28: input.trustedGeometry.right=3836; input.currentEnvelope.right=3836; break;
                case 29: input.trustedGeometry.top=0; input.currentEnvelope.top=0; break;
                case 30: input.trustedGeometry.bottom=2160; input.currentEnvelope.bottom=2160; break;
                case 31: input.currentEnvelope.right=3836; break;
                }
                const auto message=L"retiring inspection fault="+std::to_wstring(fault);
                Assert::IsFalse(CanInspectRetiringTranslationFit(input,fit,blocked),message.c_str());
            }
        }

        TEST_METHOD(FreshConfirmedFitRetiresExpiredTranslationAndReachesBoundedPresentation)
        {
            auto update=FreshFitAtTranslationExpiry();
            VerticalBarContentDecision dense; dense.action=VerticalBarPresentationAction::FIT;
            const auto previousFit=ConfirmVerticalFit(ConfirmVerticalFit({},dense,98).state,dense,99);
            const auto fresh=ConfirmVerticalFit(previousFit.state,dense,update.currentSourceSequence);
            Assert::IsFalse(fresh.pending);
            Assert::AreEqual(update.currentSourceSequence,fresh.state.lastObservedSourceSequence);
            update.current=fresh.effective;
            const auto state=UpdateVerticalBarPresentation(update);
            Assert::IsTrue(state.action==VerticalBarPresentationAction::FIT,
                L"A fresh confirmed two-edge FIT may retire the expired translation despite previousOwnsCurrentAnalysis.");
            Assert::AreEqual(0.0f,state.translationPixels);
            Assert::AreEqual(54,state.detectedTop); Assert::AreEqual(2106,state.detectedBottom);
            Assert::AreEqual(update.currentTick,state.lastDetectionTick);
            Assert::AreEqual(update.currentSourceSequence,state.sourceSequence);

            VerticalBarPresentationResolutionInput resolution;
            resolution.detailedAction=state.action; resolution.translationPixels=state.translationPixels;
            resolution.genericUpperExpansion=resolution.genericLowerExpansion=true;
            resolution.genericUpperBound=state.detectedTop; resolution.genericLowerBound=state.detectedBottom;
            resolution.authoritativeTop=276; resolution.authoritativeBottom=1884; resolution.rasterHeight=2160;
            const auto routing=ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(resolution));
            Assert::IsTrue(routing.fitActive); Assert::IsFalse(routing.translationActive);
            Assert::AreEqual(0,routing.translationPixels);
            auto crop=TrustedScopeCrop();
            crop.geometry=ExpiredTranslationWithTwoEdgeEnvelope().trustedGeometry;
            crop.frameSourceSequence=100;
            const auto admitted=AdmitCropPresentation({},crop,Evaluate(crop),9).state;
            crop.outwardPresentationActive=routing.fitActive;
            crop.outwardExpansionAvailable=true;
            crop.outwardExpansion=ExpiredTranslationWithTwoEdgeEnvelope().currentEnvelope;
            crop.outwardExpansionSourceGeneration=7;
            // Reproduce the real provisional authority gap, not a trusted new AR.
            crop.latestObservationSupportsCrop=false;
            crop.latestObservationIsProvisional=true;
            crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated=true;
            crop.frameLocalPresentationRetentionSafe=false;
            VerticalInspectionFitResolutionInput fitResolution;
            fitResolution.confirmedDenseFit=state.action==VerticalBarPresentationAction::FIT;
            fitResolution.denseAnalysisCurrent=fresh.state.lastObservedSourceSequence==crop.frameSourceSequence;
            fitResolution.outwardExpansionAvailable=true;
            fitResolution.trustedBase=crop.geometry;
            fitResolution.outwardExpansion=crop.outwardExpansion;
            fitResolution.outwardExpansionSourceGeneration=fitResolution.frameSourceGeneration=7;
            VerticalInspectionBridgeInput bridge;
            bridge.candidate=bridge.retentionRequested=true;
            bridge.denseAnalysisCompleted=true;
            bridge.verticalPresentationOwnerAvailable=true;
            bridge.confirmedVerticalFitResolved=CanResolveVerticalInspectionWithConfirmedFit(fitResolution);
            bridge.sourceGeneration=7; bridge.presentationEpoch=9;
            bridge.trustedBase=crop.geometry; bridge.sourceSequence=crop.frameSourceSequence;
            Assert::IsTrue(bridge.confirmedVerticalFitResolved);
            const auto inspected=UpdateVerticalInspectionBridge(bridge);
            Assert::IsFalse(inspected.retain);
            Assert::IsFalse(inspected.state.failOpenLatched);
            const auto final=AdmitCropPresentation(admitted,crop,Evaluate(crop),9);
            Assert::IsFalse(final.blocked);
            Assert::IsTrue(final.presentation.applyCrop && final.presentation.outwardExpanded);
            Assert::IsFalse(final.presentation.verticallyTranslated);
            Assert::AreEqual(0,final.presentation.sourceBounds.left); Assert::AreEqual(3840,final.presentation.sourceBounds.right);
            Assert::AreEqual(54,final.presentation.sourceBounds.top); Assert::AreEqual(2106,final.presentation.sourceBounds.bottom);
            Assert::AreEqual(276,final.state.trustedCrop.top);
            Assert::AreEqual(1884,final.state.trustedCrop.bottom);
        }

        TEST_METHOD(RetiringTranslationFlagCannotOverrideUnexpiredOrUnprovenFit)
        {
            for (int fault=0;fault<10;++fault)
            {
                auto update=FreshFitAtTranslationExpiry();
                switch (fault)
                {
                case 0: update.retiringTranslationFitInspection=false; break;
                case 1: update.currentTick=3000; break;
                case 2: update.holdMs=0; break;
                case 3: update.currentTick=999; break;
                case 4: update.previous.lastDetectionTick=0; break;
                case 5: update.currentSourceSequence=update.previous.sourceSequence; break;
                case 6: update.currentSourceSequence=update.previous.sourceSequence-1; break;
                case 7: update.currentSourceSequence=0; break;
                case 8: update.upperContent=false; break;
                case 9: update.lowerContent=false; break;
                }
                const auto state=UpdateVerticalBarPresentation(update);
                const auto message=L"retiring update fault="+std::to_wstring(fault);
                Assert::IsTrue(state.action==VerticalBarPresentationAction::TRANSLATE,message.c_str());
                Assert::AreEqual(178.0f,state.translationPixels,message.c_str());
                Assert::AreEqual(uint64_t{50},state.sourceSequence,message.c_str());
            }
            for (bool oneEdge : {false,true})
            {
                auto update=FreshFitAtTranslationExpiry();
                update.current.action=oneEdge ? VerticalBarPresentationAction::TRANSLATE : VerticalBarPresentationAction::NONE;
                update.current.translationPixels=oneEdge ? 184.0f : 0.0f;
                update.upperContent=false;
                const auto state=UpdateVerticalBarPresentation(update);
                Assert::IsFalse(state.action==VerticalBarPresentationAction::FIT,
                    L"Fresh negative or one-edge subtitle evidence cannot be promoted into FIT by an inspection request.");
                if (oneEdge) Assert::AreEqual(184.0f,state.translationPixels);
                else Assert::IsTrue(state.action==VerticalBarPresentationAction::NONE);
            }
        }

		TEST_METHOD(PersistentSubtitleMayBeRescannedOnHeldTrustedBarGeometry)
		{
			HeldBarAnalysisInput input;
			input.trustedBarGeometryAvailable = true;
			input.storedBaseMatchesTrustedGeometry = true;
			input.currentEnvelopeAvailable = true;
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			input.trustedGeometry = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.bottom = 2042;
			input.currentEnvelope.aspectRatio = 3840.0 / 1766.0;
			input.presentation.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.presentation.translationPixels = 199.0f;
			input.presentation.lastDetectionTick = 1000;
			input.presentation.sourceSequence = 101;
			input.evidenceSourceGeneration = 7;
			input.currentSourceGeneration = 7;
			input.currentTick = 2999;
			input.holdMs = 2000;
			input.currentSourceSequence = 220;
			Assert::IsTrue(CanAnalyzeHeldVerticalBarGeometry(input));

			// A scheduled dense scan refreshes the same subtitle before its release
			// lease expires, allowing a persistent cue to remain active indefinitely
			// without granting provisional geometry crop authority.
			VerticalBarPresentationUpdateInput refresh;
			refresh.previous = input.presentation;
			refresh.current.action =
				VerticalBarPresentationAction::TRANSLATE;
			refresh.current.translationPixels = 199.0f;
			refresh.lowerContent = true;
			refresh.lowerContentBottom = 2042;
			refresh.currentTick = 2999;
			refresh.currentSourceSequence = 220;
			refresh.holdMs = 2000;
			refresh.translationEnabled = true;
			const auto refreshed = UpdateVerticalBarPresentation(refresh);
			Assert::IsTrue(IsVerticalBarPresentationActive(
				refreshed, 4998, 2000, 221));
			Assert::IsFalse(IsVerticalBarPresentationActive(
				refreshed, 5000, 2000, 221));
		}

		TEST_METHOD(HeldBarAnalysisRejectsContradictoryOrStaleEvidence)
		{
			HeldBarAnalysisInput input;
			input.trustedBarGeometryAvailable = true;
			input.storedBaseMatchesTrustedGeometry = true;
			input.currentEnvelopeAvailable = true;
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			input.trustedGeometry = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.bottom = 2042;
			input.presentation.action =
				VerticalBarPresentationAction::TRANSLATE;
			input.presentation.translationPixels = 199.0f;
			input.presentation.lastDetectionTick = 1000;
			input.presentation.sourceSequence = 101;
			input.evidenceSourceGeneration = 7;
			input.currentSourceGeneration = 7;
			input.currentTick = 1100;
			input.holdMs = 2000;
			input.currentSourceSequence = 102;
			Assert::IsTrue(CanAnalyzeHeldVerticalBarGeometry(input));

			input.currentEnvelopeAvailable = false; // subtitle disappeared
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.currentEnvelopeAvailable = true;
			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.top = 150; // only the opposite edge changed
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.bottom = 2042;
			input.currentSourceGeneration = 8; // source replacement
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.currentSourceGeneration = 7;
			input.latestClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.latestClassification =
				ActivePictureClassification::UNAVAILABLE;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			input.currentBarAuthority = true; // fallback is unnecessary
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
		}

		TEST_METHOD(PendingTranslationMayConfirmAcrossMatchingAuthorityGap)
		{
			HeldBarAnalysisInput input;
			input.trustedBarGeometryAvailable = true;
			input.storedBaseMatchesTrustedGeometry = true;
			input.currentEnvelopeAvailable = true;
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			input.trustedGeometry = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.bottom = 2022;
			input.currentEnvelope.aspectRatio = 3840.0 / 1746.0;
			input.translationConfirmationPending = true;
			input.pendingTranslationPixels = 192.0f;
			input.evidenceSourceGeneration = 7;
			input.currentSourceGeneration = 7;
			Assert::IsTrue(CanAnalyzeHeldVerticalBarGeometry(input));

			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.top = 100;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.bottom = 2022;
			input.currentSourceGeneration = 8;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.currentSourceGeneration = 7;
			input.latestClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
		}

		TEST_METHOD(PendingFitMayConfirmAcrossMatchingTwoEdgeAuthorityGap)
		{
			HeldBarAnalysisInput input;
			input.trustedBarGeometryAvailable = true;
			input.storedBaseMatchesTrustedGeometry = true;
			input.currentEnvelopeAvailable = true;
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			input.trustedGeometry = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			input.currentEnvelope = input.trustedGeometry;
			input.currentEnvelope.top = 100;
			input.currentEnvelope.bottom = 2040;
			input.fitConfirmationPending = true;
			input.evidenceSourceGeneration = 7;
			input.currentSourceGeneration = 7;
			Assert::IsTrue(CanAnalyzeHeldVerticalBarGeometry(input));

			input.currentEnvelope.top = input.trustedGeometry.top;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.currentEnvelope.top = 100;
			input.currentEnvelope.left = 20;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
			input.currentEnvelope.left = 0;
			input.currentSourceGeneration = 8;
			Assert::IsFalse(CanAnalyzeHeldVerticalBarGeometry(input));
		}

		TEST_METHOD(CurrentEnvelopeAttestsHeldTranslationWithoutRefreshingItsFrame)
		{
			const ActivePictureBounds trusted = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			VerticalBarPresentationState held;
			held.action = VerticalBarPresentationAction::TRANSLATE;
			held.translationPixels = 128.0f;
			held.sourceSequence = 4590; // A rejected Fit must not refresh this.

			ActivePictureBounds current = trusted;
			current.bottom = 1982;
			current.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			Assert::IsTrue(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));
			current.bottom = 2012;
			Assert::IsTrue(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));
			current.bottom = 2014;
			Assert::IsFalse(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));

			// Mixed/orthogonal expansion is not an overlay attestation.
			current.top = 218;
			Assert::IsFalse(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));
			current.top = trusted.top;
			current.left = 10;
			Assert::IsFalse(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));

			// Direction must match the one and only expanded edge.
			current = trusted;
			current.top = 218;
			held.translationPixels = -128.0f;
			Assert::IsTrue(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));
			current.top = 148;
			Assert::IsTrue(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));
			current.top = 146;
			Assert::IsFalse(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));
			current.top = 218;
			current.bottom = 1982;
			Assert::IsFalse(CurrentTranslationEnvelopeSupportsGeometry(
				held, trusted, current));
		}

		TEST_METHOD(OutwardLogicalGeometryRequiresBroadOpposingSameFramePicture)
		{
			const ActivePictureBounds scope = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE };
			ActivePicturePresentationRetentionEvidence evidence;
			evidence.analysisValid = evidence.presentationValid = true;
			evidence.excludedTop.barPixels = 276;
			evidence.excludedBottom.barPixels = 276;

			// Localized top/bottom UI may make both bars non-black, but its high
			// continuity/black fraction is not broad replacement picture.
			evidence.excludedTop.blackFraction = 0.92;
			evidence.excludedTop.continuity = 0.95;
			evidence.excludedTop.lumaP90 = 500.0;
			evidence.excludedBottom = evidence.excludedTop;
			auto decision = ConfirmOutwardPictureTransition({}, scope, full,
				evidence, 7);
			Assert::IsTrue(decision.outwardTransition);
			Assert::IsFalse(decision.broadOpposingPicture);
			Assert::IsFalse(decision.authoritative);

			evidence.excludedTop.blackFraction = 0.30;
			evidence.excludedTop.continuity = 0.40;
			evidence.excludedBottom = evidence.excludedTop;
			OutwardPictureConfirmationState state;
			for (uint32_t sample = 1;
				sample <= OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED; ++sample)
			{
				decision = ConfirmOutwardPictureTransition(state, scope, full,
					evidence, 7);
				state = decision.state;
				Assert::AreEqual(sample, state.confirmations);
				Assert::AreEqual(
					sample == OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED,
					decision.authoritative);
			}

			// Proof is same-generation and consecutive.
			decision = ConfirmOutwardPictureTransition(state, scope, full,
				evidence, 8);
			Assert::AreEqual(1U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);

			evidence.excludedBottom.blackFraction = 0.92;
			evidence.excludedBottom.continuity = 0.95;
			decision = ConfirmOutwardPictureTransition({}, scope, full,
				evidence, 7);
			Assert::IsFalse(decision.broadOpposingPicture);

			evidence.excludedBottom = evidence.excludedTop;
			evidence.excludedBottom.blackFraction = 0.30;
			evidence.excludedBottom.continuity = 0.40;
			ActivePictureBounds jittered = full;
			jittered.top = 2;
			jittered.aspectRatio = 3840.0 / 2158.0;
			state = ConfirmOutwardPictureTransition({}, scope, jittered,
				evidence, 7).state;
			decision = ConfirmOutwardPictureTransition(state, scope, full,
				evidence, 7);
			Assert::AreEqual(1U, decision.state.confirmations);
		}

		TEST_METHOD(DarkNativeDistributedPictureRequiresThreeFreshExactFrames)
		{
			const ActivePictureBounds scope = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds candidate = {
				0, 68, 3840, 2092, 3840, 2160, 3840.0 / 2024.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			auto makeEvidence = [&]()
			{
				ActivePicturePresentationRetentionEvidence evidence;
				evidence.analysisValid = evidence.presentationValid = true;
				evidence.expansionStripsAvailable = true;
				evidence.expansionBase = scope;
				evidence.expansionCandidate = candidate;
				auto setDarkDistributedStrip = [](ActivePictureEdgeEvidence& edge, int pixels)
				{
					edge.barPixels = pixels;
					edge.blackFraction = 0.04;
					edge.continuity = 0.0;
					edge.lumaP90 = 36.0;
					edge.texture = 7.1;
					edge.distributedSupportDepthRowsByQuartile.fill(0x3f);
				};
				setDarkDistributedStrip(evidence.expandingTop, scope.top - candidate.top);
				setDarkDistributedStrip(evidence.expandingBottom, candidate.bottom - scope.bottom);
				evidence.activePicture.available = true;
				evidence.activePicture.classification =
					ActivePictureClassification::BAR_CROP_TRUSTED;
				evidence.activePicture.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
				evidence.activePicture.trustedBounds = candidate;
				evidence.activePicture.top.trusted = true;
				evidence.activePicture.bottom.trusted = true;
				evidence.activePicture.axisEvidence.vertical.scanComplete = true;
				evidence.activePicture.axisEvidence.vertical.state =
					ActivePictureAxisState::TRUSTED_BARS;
				return evidence;
			};
			auto evidence = makeEvidence();
			OutwardPictureConfirmationState state;
			auto decision = ConfirmOutwardPictureTransition(state, scope, candidate,
				evidence, 7, 0);
			Assert::IsFalse(decision.nativeDistributedPicture);
			Assert::AreEqual(0U, decision.state.confirmations);
			decision = ConfirmOutwardPictureTransition(state, scope, candidate,
				evidence, 7, 100);
			Assert::IsFalse(decision.broadOpposingPicture);
			Assert::IsTrue(decision.nativeDistributedPicture);
			Assert::AreEqual(1U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);

			decision = ConfirmOutwardPictureTransition(decision.state, scope, candidate,
				evidence, 7, 100);
			Assert::AreEqual(1U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);
			decision = ConfirmOutwardPictureTransition(decision.state, scope, candidate,
				evidence, 7, 101);
			Assert::AreEqual(2U, decision.state.confirmations);
			decision = ConfirmOutwardPictureTransition(decision.state, scope, candidate,
				evidence, 7, 103);
			Assert::AreEqual(1U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);
			decision = ConfirmOutwardPictureTransition(decision.state, scope, candidate,
				evidence, 8, 104);
			Assert::AreEqual(1U, decision.state.confirmations);
			decision = ConfirmOutwardPictureTransition(decision.state, scope, candidate,
				evidence, 8, 105);
			decision = ConfirmOutwardPictureTransition(decision.state, scope, candidate,
				evidence, 8, 106);
			Assert::AreEqual(3U, decision.state.confirmations);
			Assert::IsTrue(decision.authoritative);
		}

		TEST_METHOD(NativeDistributedProofRejectsLocalizedOrUnownedEvidenceAndCannotMixRoutes)
		{
			const ActivePictureBounds scope = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds candidate = {
				0, 68, 3840, 2092, 3840, 2160, 3840.0 / 2024.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			ActivePicturePresentationRetentionEvidence evidence;
			evidence.analysisValid = evidence.presentationValid = true;
			evidence.expansionStripsAvailable = true;
			evidence.expansionBase = scope;
			evidence.expansionCandidate = candidate;
			auto setDarkDistributedStrip = [](ActivePictureEdgeEvidence& edge, int pixels)
			{
				edge.barPixels = pixels;
				edge.blackFraction = 0.04;
				edge.continuity = 0.0;
				edge.lumaP90 = 36.0;
				edge.texture = 7.1;
				edge.distributedSupportDepthRowsByQuartile.fill(0x3f);
			};
			setDarkDistributedStrip(evidence.expandingTop, scope.top - candidate.top);
			setDarkDistributedStrip(evidence.expandingBottom, candidate.bottom - scope.bottom);
			evidence.activePicture.available = true;
			evidence.activePicture.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
			evidence.activePicture.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
			evidence.activePicture.trustedBounds = candidate;
			evidence.activePicture.top.trusted = evidence.activePicture.bottom.trusted = true;
			evidence.activePicture.axisEvidence.vertical.scanComplete = true;
			evidence.activePicture.axisEvidence.vertical.state = ActivePictureAxisState::TRUSTED_BARS;

			evidence.expandingTop.distributedSupportDepthRowsByQuartile[2] = 0x01;
			auto decision = ConfirmOutwardPictureTransition({}, scope, candidate, evidence, 7, 100);
			Assert::IsFalse(decision.nativeDistributedPicture);
			Assert::AreEqual(0U, decision.state.confirmations);
			evidence.expandingTop.distributedSupportDepthRowsByQuartile.fill(0x3f);
			auto rejectsNative = [&](ActivePicturePresentationRetentionEvidence invalid)
			{
				const auto rejected = ConfirmOutwardPictureTransition({}, scope,
				candidate, invalid, 7, 100);
				Assert::IsFalse(rejected.nativeDistributedPicture);
				Assert::AreEqual(0U, rejected.state.confirmations);
			};
			auto invalid = evidence; invalid.analysisValid = false; rejectsNative(invalid);
			invalid = evidence; invalid.presentationValid = false; rejectsNative(invalid);
			invalid = evidence; invalid.globalNearBlack = true; rejectsNative(invalid);
			invalid = evidence; invalid.expansionStripsAvailable = false; rejectsNative(invalid);
			invalid = evidence; invalid.expansionCandidate.bottom--; rejectsNative(invalid);
			invalid = evidence; invalid.activePicture.classification = ActivePictureClassification::PROVISIONAL; rejectsNative(invalid);
			invalid = evidence; invalid.activePicture.authorityOrigin = ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT; rejectsNative(invalid);
			invalid = evidence; invalid.activePicture.trustedBounds.top++; rejectsNative(invalid);
			invalid = evidence; invalid.activePicture.top.trusted = false; rejectsNative(invalid);
			invalid = evidence; invalid.activePicture.bottom.trusted = false; rejectsNative(invalid);
			invalid = evidence; invalid.activePicture.axisEvidence.vertical.scanComplete = false; rejectsNative(invalid);
			invalid = evidence; invalid.activePicture.axisEvidence.vertical.state = ActivePictureAxisState::UNKNOWN; rejectsNative(invalid);



			decision = ConfirmOutwardPictureTransition({}, scope, candidate, evidence, 7, 100);
			Assert::AreEqual(1U, decision.state.confirmations);
			auto broad = evidence;
			broad.expandingTop.blackFraction = broad.expandingBottom.blackFraction = 0.30;
			broad.expandingTop.continuity = broad.expandingBottom.continuity = 0.40;
			broad.expandingTop.lumaP90 = broad.expandingBottom.lumaP90 = 200.0;
			auto switched = ConfirmOutwardPictureTransition(decision.state, scope, candidate,
				broad, 7, 101);
			Assert::IsTrue(switched.broadOpposingPicture);
			Assert::AreEqual(1U, switched.state.confirmations);
			decision = ConfirmOutwardPictureTransition(switched.state, scope, candidate,
				evidence, 7, 102);
			Assert::IsTrue(decision.nativeDistributedPicture);
			Assert::AreEqual(1U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);
		}

		TEST_METHOD(CadenceRepeatCannotAdvanceOutwardPictureConfirmation)
		{
			const ActivePictureBounds scope = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE };
			ActivePicturePresentationRetentionEvidence evidence;
			evidence.analysisValid = evidence.presentationValid = true;
			evidence.excludedTop.barPixels = 276;
			evidence.excludedTop.blackFraction = 0.30;
			evidence.excludedTop.continuity = 0.40;
			evidence.excludedTop.lumaP90 = 500.0;
			evidence.excludedBottom = evidence.excludedTop;

			OutwardPictureConfirmationState state;
			auto decision = ConfirmOutwardPictureTransition(
				state, scope, full, evidence, 7, 100);
			Assert::AreEqual(1U, decision.state.confirmations);
            Assert::AreEqual("proof-pending",decision.diagnosticReason);

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 100);
			Assert::AreEqual(1U, decision.state.confirmations);
            Assert::AreEqual("repeated-source",decision.diagnosticReason);
			Assert::IsFalse(decision.authoritative);

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 101);
			Assert::AreEqual(2U, decision.state.confirmations);
            Assert::AreEqual("proof-pending",decision.diagnosticReason);
			Assert::IsFalse(decision.authoritative);

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 101);
			Assert::AreEqual(2U, decision.state.confirmations);
            Assert::AreEqual("repeated-source",decision.diagnosticReason);
			Assert::IsFalse(decision.authoritative);

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 102);
			Assert::AreEqual(3U, decision.state.confirmations);
            Assert::AreEqual("confirmed",decision.diagnosticReason);
			Assert::IsTrue(decision.authoritative);
		}

		TEST_METHOD(FreshOneEdgeOverlayKeepsHeldFitUntilItsBoundedRelease)
		{
			VerticalBarPresentationUpdateInput update;
			update.previous.action = VerticalBarPresentationAction::FIT;
			update.previous.detectedTop = 80;
			update.previous.detectedBottom = 2080;
			update.previous.lastDetectionTick = 1000;
			update.previous.sourceSequence = 40;
			update.current.action = VerticalBarPresentationAction::TRANSLATE;
			update.current.translationPixels = -75.0f;
			update.upperContent = true;
			update.upperContentTop = 104;
			update.currentTick = 1100;
			update.currentSourceSequence = 41;
			update.holdMs = 2000;
			update.translationEnabled = true;
			auto state = UpdateVerticalBarPresentation(update);
			Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT), static_cast<int>(state.action));
			Assert::AreEqual(80, state.detectedTop);
			Assert::AreEqual(2080, state.detectedBottom);
			// A newly appearing control can grow the envelope immediately.
			update.previous=state;update.currentSourceSequence=42;update.currentTick=1200;
			update.upperContentTop=60;
			state=UpdateVerticalBarPresentation(update);
			Assert::AreEqual(60,state.detectedTop);
			Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT), static_cast<int>(state.action));
			// Repeated source samples do not renew the hold.
			update.previous=state;update.currentTick=1300;
			state=UpdateVerticalBarPresentation(update);
			Assert::AreEqual(uint64_t(1200),state.lastDetectionTick);
			// A completed negative releases only after the existing timer.
			update.previous=state;update.current={};update.upperContent=false;
			update.currentTick=1500;update.currentSourceSequence=43;
			Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT),
				static_cast<int>(UpdateVerticalBarPresentation(update).action));
			update.currentTick=3201;update.currentSourceSequence=44;
			Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::NONE),
				static_cast<int>(UpdateVerticalBarPresentation(update).action));
			// A genuinely expired FIT may adopt fresh one-edge placement.
			update.current.action=VerticalBarPresentationAction::TRANSLATE;
			update.current.translationPixels=-75.0f;update.upperContent=true;
			Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(UpdateVerticalBarPresentation(update).action));
		}

		TEST_METHOD(LiveOverlayAndAspectMetadataTimelineProducesStableFinalGeometry)
		{
			// Compressed replay of the 2026-08-05 live metadata. Ticks advance
			// synthetically, so seconds of hold/release behavior execute instantly.
			ActivePictureBounds geometry = {
				0, 280, 3840, 1888, 3840, 2160,
				3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			VerticalBarPresentationState state;
			const uint64_t generation = 7;
			const uint64_t holdMs = 2000;
			auto update = [&](const VerticalBarContentDecision& current,
				bool upper, bool lower, int upperTop, int lowerBottom,
				uint64_t tick, uint64_t sequence)
			{
				VerticalBarPresentationUpdateInput input;
				input.previous = state;
				input.current = current;
				input.upperContent = upper;
				input.lowerContent = lower;
				input.upperContentTop = upperTop;
				input.lowerContentBottom = lowerBottom;
				input.currentTick = tick;
				input.currentSourceSequence = sequence;
				input.holdMs = holdMs;
				input.translationEnabled = true;
				state = UpdateVerticalBarPresentation(input);
			};
			auto resolve = [&](bool genericTop, int genericTopBound,
				bool genericBottom, int genericBottomBound)
			{
				VerticalBarPresentationResolutionInput input;
				input.detailedAction = state.action;
				input.translationPixels = state.translationPixels;
				input.genericUpperExpansion = genericTop;
				input.genericLowerExpansion = genericBottom;
				input.genericUpperBound = genericTopBound;
				input.genericLowerBound = genericBottomBound;
				input.authoritativeTop = geometry.top;
				input.authoritativeBottom = geometry.bottom;
				input.rasterHeight = 2160;
				return ResolveVerticalBarPresentation(input);
			};
			auto present = [&](const VerticalBarPresentationResolution& action,
				const ActivePictureBounds& outward)
			{
				Input input;
				input.automaticCropEnabled = true;
				input.sharedGeometryAvailable = true;
				input.latestObservationSupportsCrop = true;
				input.classification =
					ActivePictureClassification::BAR_CROP_TRUSTED;
				input.geometry = geometry;
				input.geometrySourceGeneration = generation;
				input.frameSourceGeneration = generation;
				input.rasterWidth = 3840;
				input.rasterHeight = 2160;
				input.presentationFailOpen = action.action ==
					VerticalBarPresentationAction::FAIL_OPEN;
				input.verticalTranslationActive = action.action ==
					VerticalBarPresentationAction::TRANSLATE;
				input.verticalTranslationPixels = action.translationPixels < 0.0f
					? static_cast<int>(std::floor(action.translationPixels))
					: static_cast<int>(std::ceil(action.translationPixels));
				input.verticalTranslationBase = geometry;
				input.verticalTranslationSourceGeneration = generation;
				input.outwardPresentationActive = action.action ==
					VerticalBarPresentationAction::FIT;
				input.outwardExpansionAvailable =
					input.outwardPresentationActive;
				input.outwardExpansion = outward;
				input.outwardExpansionSourceGeneration = generation;
				return Evaluate(input);
			};

			// Stable scope authority: ordinary presentation, no overlay action.
			auto action = resolve(false, geometry.top, false, geometry.bottom);
			auto crop = present(action, geometry);
			Assert::AreEqual(280, crop.sourceBounds.top);
			Assert::AreEqual(1888, crop.sourceBounds.bottom);

			// Bottom subtitle from the live log: +75, then +91 as its second line
			// appears. Scale/aspect remain unchanged; only the source window moves.
			VerticalBarContentInput content;
			content.lowerContent = true;
			content.lowerOccupiedDepth = 40;
			content.lowerPeakSamples = 220;
			content.upperBarPixels = 280;
			content.lowerBarPixels = 272;
			content.sampledColumns = 1800;
			content.lowerRequiredShift = 75.0f;
			update(EvaluateVerticalBarContent(content), false, true,
				0, 1918, 1000, 101);
			action = resolve(false, geometry.top, false, geometry.bottom);
			crop = present(action, geometry);
			Assert::IsTrue(crop.verticallyTranslated);
			Assert::AreEqual(356, crop.sourceBounds.top);
			Assert::AreEqual(1964, crop.sourceBounds.bottom);
			Assert::AreEqual(1608, crop.sourceBounds.bottom -
				crop.sourceBounds.top);

			content.lowerRequiredShift = 91.0f;
			update(EvaluateVerticalBarContent(content), false, true,
				0, 1934, 1050, 102);
			action = resolve(false, geometry.top, false, geometry.bottom);
			crop = present(action, geometry);
			Assert::AreEqual(372, crop.sourceBounds.top);
			Assert::AreEqual(1980, crop.sourceBounds.bottom);
			const double translatedAspect = 3840.0 / 1608.0;
			const auto mapping = EvaluateNlsMapping(true,
				translatedAspect, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(static_cast<int>(
				NlsMappingMode::LINEAR_PASSTHROUGH),
				static_cast<int>(mapping.mode));

			// Reproduce the two-edge Eternals sequence: sparse top UI appears while
			// the bottom subtitle grows. Keep the scope-sized source window, retain
			// the subtitle direction, and reveal the larger lower cue immediately.
			content.upperContent = true;
			content.upperOccupiedDepth = 70;
			content.upperPeakSamples = 133;
			content.upperRequiredShift = 153.0f;
			content.lowerOccupiedDepth = 90;
			content.lowerPeakSamples = 238;
			content.lowerRequiredShift = 199.0f;
			content.bottomTranslationHeld = true;
			const auto simultaneousOverlays =
				EvaluateVerticalBarContent(content);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(simultaneousOverlays.action));
			Assert::AreEqual(199.0f,
				simultaneousOverlays.translationPixels, 0.001f);
			update(simultaneousOverlays, true, true, 164, 2030, 1100, 103);
			action = resolve(true, 164, true, 2030);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(action.action));
			crop = present(action, geometry);
			Assert::IsTrue(crop.verticallyTranslated);
			Assert::IsFalse(crop.outwardExpanded);
			Assert::AreEqual(480, crop.sourceBounds.top);
			Assert::AreEqual(2088, crop.sourceBounds.bottom);
			Assert::AreEqual(1608, crop.sourceBounds.bottom -
				crop.sourceBounds.top);

			// No new analysis inside the release hold: keep the exact placement.
			update({}, false, false, 0, 0, 1500, 104);
			action = resolve(false, geometry.top, false, geometry.bottom);
			crop = present(action, geometry);
			Assert::AreEqual(480, crop.sourceBounds.top);
			// Once the accelerated hold expires, return directly to stable scope.
			update({}, false, false, 0, 0, 3201, 105);
			action = resolve(false, geometry.top, false, geometry.bottom);
			crop = present(action, geometry);
			Assert::AreEqual(280, crop.sourceBounds.top);

			// Thin full-width top volume UI is overlay-like, not an aspect change.
			content = {};
			content.upperContent = true;
			content.upperOccupiedDepth = 40;
			content.upperPeakSamples = 1500;
			content.upperBarPixels = 280;
			content.lowerBarPixels = 272;
			content.sampledColumns = 1800;
			content.upperRequiredShift = 75.0f;
			update(EvaluateVerticalBarContent(content), true, false,
				220, 0, 3300, 106);
			action = resolve(true, 220, false, geometry.bottom);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(action.action));
			crop = present(action, geometry);
			Assert::AreEqual(204, crop.sourceBounds.top);
			Assert::AreEqual(1812, crop.sourceBounds.bottom);

			// Coarse current evidence is classification input, not a new motion
			// target. Keep the dense volume/UI pass position stable.
			action = resolve(true, 120, false, geometry.bottom);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(action.action));
			Assert::AreEqual(-76.0f, action.translationPixels, 0.001f);
			crop = present(action, geometry);
			Assert::IsFalse(crop.outwardExpanded);
			Assert::IsTrue(crop.verticallyTranslated);
			Assert::AreEqual(204, crop.sourceBounds.top);
			Assert::AreEqual(1812, crop.sourceBounds.bottom);

			// Broad/deep pixels on both bars normally select FIT, but a currently
			// active volume/subtitle placement has bounded presentation precedence.
			// The next real aspect change must arrive as trusted source authority.
			content.lowerContent = true;
			content.upperOccupiedDepth = 220;
			content.lowerOccupiedDepth = 220;
			content.lowerPeakSamples = 1500;
			content.lowerRequiredShift = 180.0f;
			update(EvaluateVerticalBarContent(content), true, true,
				60, 2100, 3350, 107);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(state.action));
			action = resolve(true, 60, true, 2100);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(action.action));
			crop = present(action, geometry);
			Assert::IsTrue(crop.verticallyTranslated);
			Assert::AreEqual(1608,
				crop.sourceBounds.bottom - crop.sourceBounds.top);

			// Invalid bar metadata is an explicit full-raster fail-open.
			content = {};
			content.lowerContent = true;
			content.lowerRequiredShift = 75.0f;
			update(EvaluateVerticalBarContent(content), false, true,
				0, 1918, 3400, 108);
			action = resolve(false, geometry.top, false, geometry.bottom);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FAIL_OPEN),
				static_cast<int>(action.action));
			AssertFullRaster(present(action, geometry));

			// A genuine IMAX transition arrives as new trusted picture authority,
			// not overlay metadata; clearing the old presentation state is immediate.
			state = {};
			geometry = { 0, 70, 3840, 2090, 3840, 2160,
				3840.0 / 2020.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			action = resolve(false, geometry.top, false, geometry.bottom);
			crop = present(action, geometry);
			Assert::AreEqual(70, crop.sourceBounds.top);
			Assert::AreEqual(2090, crop.sourceBounds.bottom);
		}

		TEST_METHOD(FullRasterAuthorityBridgesSparseAnalysisWithoutWaitingPulse)
		{
			bool authority = UpdateFullRasterPresentationAuthority(false,
				ActivePictureClassification::FULL_RASTER_TRUSTED, true);
			Assert::IsTrue(authority);

			// Normal frames between detector samples carry no new classification.
			for (int frame = 0; frame < 120; ++frame)
			{
				authority = UpdateFullRasterPresentationAuthority(authority,
					ActivePictureClassification::UNAVAILABLE, false);
				Assert::IsTrue(authority);
			}
			Input crop = TrustedScopeCrop();
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsProvisional = true;
			crop.frameLocalPresentationRetentionEvaluated = true;
			crop.frameLocalPresentationRetentionSafe = true;
			crop.fullRasterPresentationAuthoritative = authority;
			AssertFullRaster(Evaluate(crop));

			// A malformed full-raster claim and trusted bar evidence both revoke it.
			authority = UpdateFullRasterPresentationAuthority(authority,
				ActivePictureClassification::FULL_RASTER_TRUSTED, false);
			Assert::IsFalse(authority);
			authority = UpdateFullRasterPresentationAuthority(true,
				ActivePictureClassification::BAR_CROP_TRUSTED, false);
			Assert::IsFalse(authority);
			crop.fullRasterPresentationAuthoritative = authority;
			crop.latestObservationSupportsCrop = true;
			crop.latestObservationIsProvisional = false;
			Assert::IsTrue(Evaluate(crop).applyCrop);
		}

		TEST_METHOD(ActiveCropForcesInspectionOnNonScheduledFrames)
		{
			Assert::IsTrue(RequiresPerFramePresentationInspection(
				true, false, false));
			Assert::IsTrue(RequiresPerFramePresentationInspection(
				false, true, false));
			Assert::IsTrue(RequiresPerFramePresentationInspection(
				false, false, true));
			Assert::IsFalse(RequiresPerFramePresentationInspection(
				false, false, false));
		}

		TEST_METHOD(UnsafeNewBarContentForcesOnlyTheInitialSubtitleScan)
		{
			Assert::IsTrue(RequiresImmediateSubtitleBarAnalysis(
				true, true, true, false, false));
			Assert::IsFalse(RequiresImmediateSubtitleBarAnalysis(
				false, true, true, false, false));
			Assert::IsFalse(RequiresImmediateSubtitleBarAnalysis(
				true, false, true, false, false));
			Assert::IsFalse(RequiresImmediateSubtitleBarAnalysis(
				true, true, false, false, false));
			Assert::IsFalse(RequiresImmediateSubtitleBarAnalysis(
				true, true, true, true, false));
			Assert::IsFalse(RequiresImmediateSubtitleBarAnalysis(
				true, true, true, false, true));
		}

		TEST_METHOD(CurrentFrameEnvelopeSurvivesGeometryChangeAndZeroHold)
		{
			PresentationEnvelopeInput envelope;
			envelope.envelopeAvailable = true;
			envelope.effectiveGeometryAvailable = true;
			envelope.baseMatchesEffectiveGeometry = false;
			envelope.detectedSourceSequence = 91;
			envelope.currentSourceSequence = 91;
			envelope.evidenceSourceGeneration = 7;
			envelope.frameSourceGeneration = 7;
			envelope.lastDetectionTick = 1000;
			envelope.currentTick = 1001;
			envelope.holdMs = 0;
			const PresentationEnvelopeDecision envelopeDecision =
				EvaluatePresentationEnvelope(envelope);
			Assert::IsTrue(envelopeDecision.active);
			Assert::IsTrue(envelopeDecision.currentFrame);
			Assert::IsFalse(envelopeDecision.held);

			// Model a trusted geometry publication on the same frame. The final
			// union contains both that new authority and the current overlay.
			Input crop = TrustedScopeCrop();
			crop.geometry.top = 300;
			crop.geometry.bottom = 1860;
			crop.geometry.aspectRatio = 3840.0 / 1560.0;
			crop.outwardPresentationActive = envelopeDecision.active;
			crop.outwardExpansionAvailable = true;
			crop.outwardExpansion = crop.geometry;
			crop.outwardExpansion.top = 100;
			crop.outwardExpansion.bottom = 1884;
			crop.outwardExpansion.aspectRatio = 3840.0 / 1784.0;
			crop.outwardExpansionSourceGeneration = 7;
			const Decision cropDecision = Evaluate(crop);
			Assert::IsTrue(cropDecision.applyCrop);
			Assert::IsTrue(cropDecision.outwardExpanded);
			Assert::AreEqual(100, cropDecision.sourceBounds.top);
			Assert::AreEqual(1884, cropDecision.sourceBounds.bottom);
		}

		TEST_METHOD(HeldEnvelopeRequiresMatchingBaseGenerationAndDeadline)
		{
			PresentationEnvelopeInput input;
			input.envelopeAvailable = true;
			input.effectiveGeometryAvailable = true;
			input.baseMatchesEffectiveGeometry = true;
			input.detectedSourceSequence = 90;
			input.currentSourceSequence = 91;
			input.evidenceSourceGeneration = 7;
			input.frameSourceGeneration = 7;
			input.lastDetectionTick = 1000;
			input.currentTick = 2999;
			input.holdMs = 2000;
			auto decision = EvaluatePresentationEnvelope(input);
			Assert::IsTrue(decision.active);
			Assert::IsTrue(decision.held);

			input.baseMatchesEffectiveGeometry = false;
			Assert::IsFalse(EvaluatePresentationEnvelope(input).active);
			input.baseMatchesEffectiveGeometry = true;
			input.frameSourceGeneration = 8;
			Assert::IsFalse(EvaluatePresentationEnvelope(input).active);
			input.frameSourceGeneration = 7;
			input.currentTick = 3001;
			Assert::IsFalse(EvaluatePresentationEnvelope(input).active);
			input.currentTick = 1000;
			input.holdMs = 0;
			Assert::IsFalse(EvaluatePresentationEnvelope(input).active);
		}

		TEST_METHOD(RendererWiringTranslatesOneEdgeSubtitleWithoutPillarboxing)
		{
			// Live failure: trusted movie 0,276-3840,1884 and one lower
			// subtitle extending to 1978. The required +94 displacement fits
			// completely in the raster and must not become 0,276-3840,1978.
			VerticalBarPresentationResolutionInput input;
			input.detailedAction = VerticalBarPresentationAction::TRANSLATE;
			input.translationPixels = 94.0f;
			input.genericLowerExpansion = true;
			input.genericLowerBound = 1978;
			input.authoritativeTop = 276;
			input.authoritativeBottom = 1884;
			input.rasterHeight = 2160;
			const auto resolution = ResolveVerticalBarPresentation(input);
			const auto routing = ResolveVerticalBarRendererRouting(resolution);
			Assert::IsTrue(routing.translationActive);
			Assert::IsFalse(routing.fitActive);
			Assert::AreEqual(94, routing.translationPixels);

			Input crop = TrustedScopeCrop();
			crop.geometry = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			crop.verticalTranslationActive = routing.translationActive;
			crop.verticalTranslationPixels = routing.translationPixels;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
			const Decision selected = Evaluate(crop);
			Assert::IsTrue(selected.applyCrop);
			Assert::IsTrue(selected.verticallyTranslated);
			Assert::IsFalse(selected.outwardExpanded);
			Assert::AreEqual(370, selected.sourceBounds.top);
			Assert::AreEqual(1978, selected.sourceBounds.bottom);
			Assert::AreEqual(1608,
				selected.sourceBounds.bottom - selected.sourceBounds.top);
			// Preserving scale necessarily trades the same number of rows at the
			// opposite edge. Keep that trade bounded to exactly the subtitle depth;
			// it must never become an outward aspect-changing union.
			Assert::AreEqual(94,
				selected.sourceBounds.top - crop.geometry.top);
			Assert::AreEqual(94,
				selected.sourceBounds.bottom - crop.geometry.bottom);

			const double trustedAspect = 3840.0 / 1608.0;
			const double selectedAspect = 3840.0 /
				(selected.sourceBounds.bottom - selected.sourceBounds.top);
			Assert::AreEqual(trustedAspect, selectedAspect, 0.000001);
			const PresentationRect screen = {
				0.0, 0.0, 2350.0, 1000.0 };
			const auto top = FitAspect(selectedAspect, screen,
				VerticalPictureAlignment::TOP);
			const auto center = FitAspect(selectedAspect, screen,
				VerticalPictureAlignment::CENTER);
			const auto bottom = FitAspect(selectedAspect, screen,
				VerticalPictureAlignment::BOTTOM);
			Assert::IsTrue(top.valid && center.valid && bottom.valid);
			Assert::AreEqual(static_cast<int>(UnusedSpaceAxis::VERTICAL),
				static_cast<int>(center.unusedAxis));
			Assert::AreEqual(0.0, top.picture.left, 0.001);
			Assert::AreEqual(2350.0, top.picture.right, 0.001);
			Assert::AreEqual(top.picture.right - top.picture.left,
				center.picture.right - center.picture.left, 0.001);
			Assert::AreEqual(top.picture.bottom - top.picture.top,
				bottom.picture.bottom - bottom.picture.top, 0.001);
			Assert::IsTrue(top.picture.top < center.picture.top);
			Assert::IsTrue(center.picture.top < bottom.picture.top);

			const NlsSourceGeometry trustedNlsGeometry =
				ResolveNlsSourceGeometry(true, 0, 276, 3840, 1884,
					3840, 2160);
			const NlsSourceGeometry translatedNlsGeometry =
				ResolveNlsSourceGeometry(true, selected.sourceBounds.left,
					selected.sourceBounds.top, selected.sourceBounds.right,
					selected.sourceBounds.bottom, 3840, 2160);
			Assert::IsTrue(trustedNlsGeometry.valid);
			Assert::IsTrue(translatedNlsGeometry.valid);
			Assert::AreEqual(370, translatedNlsGeometry.top);
			Assert::AreEqual(1978, translatedNlsGeometry.bottom);
			Assert::AreEqual(trustedNlsGeometry.aspect,
				translatedNlsGeometry.aspect, 0.000001);
			const auto trustedNls = EvaluateNlsMapping(
				true, trustedNlsGeometry.aspect, 2.35, 5.0, 1.0, false);
			const auto translatedNls = EvaluateNlsMapping(
				true, translatedNlsGeometry.aspect, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(trustedNls.sourceAspect,
				translatedNls.sourceAspect, 0.000001);
			Assert::AreEqual(trustedNls.requestedRatio,
				translatedNls.requestedRatio, 0.000001);

			// The VP-0098 outward union changed the source to 2.256:1 and
			// therefore selected horizontal unused space (pillarboxing).
			const auto regressedFit = FitCenteredAspect(
				3840.0 / (1978.0 - 276.0), screen);
			Assert::AreEqual(static_cast<int>(UnusedSpaceAxis::HORIZONTAL),
				static_cast<int>(regressedFit.unusedAxis));
		}

		TEST_METHOD(RendererWiringRetainsFitAndBoundarySafety)
		{
			VerticalBarPresentationResolution fit;
			fit.action = VerticalBarPresentationAction::FIT;
			const auto fitRouting = ResolveVerticalBarRendererRouting(fit);
			Assert::IsFalse(fitRouting.translationActive);
			Assert::IsTrue(fitRouting.fitActive);
			Assert::IsFalse(fitRouting.failOpen);

			VerticalBarPresentationResolutionInput blocked;
			blocked.detailedAction = VerticalBarPresentationAction::TRANSLATE;
			blocked.translationPixels = 300.0f;
			blocked.genericLowerExpansion = true;
			blocked.genericLowerBound = 2184;
			blocked.authoritativeTop = 276;
			blocked.authoritativeBottom = 1884;
			blocked.rasterHeight = 2160;
			const auto blockedResolution = ResolveVerticalBarPresentation(blocked);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::FIT),
				static_cast<int>(blockedResolution.action));
			const auto blockedRouting =
				ResolveVerticalBarRendererRouting(blockedResolution);
			Assert::IsFalse(blockedRouting.translationActive);
			Assert::IsTrue(blockedRouting.fitActive);

			VerticalBarPresentationResolution invalid;
			invalid.action = VerticalBarPresentationAction::FAIL_OPEN;
			const auto invalidRouting = ResolveVerticalBarRendererRouting(invalid);
			Assert::IsFalse(invalidRouting.translationActive);
			Assert::IsFalse(invalidRouting.fitActive);
			Assert::IsTrue(invalidRouting.failOpen);
		}

		TEST_METHOD(RendererWiringTranslatesTopEdgeAndRejectsStaleGeneration)
		{
			VerticalBarPresentationResolutionInput input;
			input.detailedAction = VerticalBarPresentationAction::TRANSLATE;
			input.translationPixels = -94.0f;
			input.genericUpperExpansion = true;
			input.genericUpperBound = 182;
			input.authoritativeTop = 276;
			input.authoritativeBottom = 1884;
			input.rasterHeight = 2160;
			const auto routing = ResolveVerticalBarRendererRouting(
				ResolveVerticalBarPresentation(input));
			Assert::IsTrue(routing.translationActive);
			Assert::AreEqual(-94, routing.translationPixels);

			Input crop = TrustedScopeCrop();
			crop.geometry = {
				0, 276, 3840, 1884, 3840, 2160, 3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			crop.verticalTranslationActive = routing.translationActive;
			crop.verticalTranslationPixels = routing.translationPixels;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
			Decision selected = Evaluate(crop);
			Assert::IsTrue(selected.verticallyTranslated);
			Assert::AreEqual(182, selected.sourceBounds.top);
			Assert::AreEqual(1790, selected.sourceBounds.bottom);

			crop.verticalTranslationSourceGeneration =
				crop.frameSourceGeneration - 1;
			selected = Evaluate(crop);
			AssertFullRaster(selected);
		}

		TEST_METHOD(PresentationEnvelopeExpandsOnlySelectedObservedEdges)
		{
			PresentationEnvelopeGeometryInput input;
			input.trustedPicture = {
				200, 280, 3640, 1880, 3840, 2160, 2.15, ActivePictureBounds::BarAxes::BOTH };
			input.observedContent = {
				111, 121, 3711, 1937, 3840, 2160, 1.9824, ActivePictureBounds::BarAxes::NONE };
			input.observedContentAvailable = true;
			input.expandLeft = true;
			input.expandBottom = true;
			input.horizontalPadding = 9;
			input.verticalPadding = 17;

			auto decision = BuildPresentationEnvelope(input);
			Assert::IsTrue(decision.valid);
			Assert::AreEqual(102, decision.bounds.left);
			Assert::AreEqual(280, decision.bounds.top);
			Assert::AreEqual(3640, decision.bounds.right);
			Assert::AreEqual(1954, decision.bounds.bottom);

			input.expandTop = true;
			input.expandRight = true;
			decision = BuildPresentationEnvelope(input);
			Assert::AreEqual(104, decision.bounds.top);
			Assert::AreEqual(3720, decision.bounds.right);
			Assert::AreEqual(0, decision.bounds.left & 1);
			Assert::AreEqual(0, decision.bounds.top & 1);
			Assert::AreEqual(0, decision.bounds.right & 1);
			Assert::AreEqual(0, decision.bounds.bottom & 1);
		}

		TEST_METHOD(CenteredFitMatrixPreservesAspectAndOneUnusedAxis)
		{
			const double screenAspects[] = {
				4.0 / 3.0, 16.0 / 9.0, 1.85, 2.0,
				32.0 / 15.0, 2.35, 2.40 };
			const double contentAspects[] = {
				4.0 / 3.0, 16.0 / 9.0, 1.85, 2.0, 2.35, 2.40 };
			for (double screenAspect : screenAspects)
			{
				const PresentationRect physicalScreen = {
					100.0, 50.0, 100.0 + screenAspect * 900.0, 950.0 };
				for (double contentAspect : contentAspects)
				{
					const CenteredFitDecision fit =
						FitCenteredAspect(contentAspect, physicalScreen);
					Assert::IsTrue(fit.valid);
					Assert::IsTrue(fit.picture.left >= physicalScreen.left - 0.001);
					Assert::IsTrue(fit.picture.top >= physicalScreen.top - 0.001);
					Assert::IsTrue(fit.picture.right <= physicalScreen.right + 0.001);
					Assert::IsTrue(fit.picture.bottom <= physicalScreen.bottom + 0.001);
					const double fittedAspect =
						(fit.picture.right - fit.picture.left) /
						(fit.picture.bottom - fit.picture.top);
					Assert::AreEqual(contentAspect, fittedAspect, 0.000001);
					const UnusedSpaceAxis expected = contentAspect > screenAspect + 1e-9
						? UnusedSpaceAxis::VERTICAL
						: (contentAspect < screenAspect - 1e-9
							? UnusedSpaceAxis::HORIZONTAL
							: UnusedSpaceAxis::NONE);
					Assert::AreEqual(static_cast<int>(expected),
						static_cast<int>(fit.unusedAxis));
				}
			}
		}

		TEST_METHOD(VerticalAlignmentRedistributesOnlyUnusedVerticalSpace)
		{
			const PresentationRect screen = { 0.0, 0.0, 1920.0, 1080.0 };
			const auto top = FitAspect(2.40, screen,
				VerticalPictureAlignment::TOP);
			const auto center = FitAspect(2.40, screen,
				VerticalPictureAlignment::CENTER);
			const auto bottom = FitAspect(2.40, screen,
				VerticalPictureAlignment::BOTTOM);

			Assert::IsTrue(top.valid && center.valid && bottom.valid);
			Assert::AreEqual(0.0, top.picture.top, 0.001);
			Assert::AreEqual(800.0, top.picture.bottom, 0.001);
			Assert::AreEqual(140.0, center.picture.top, 0.001);
			Assert::AreEqual(940.0, center.picture.bottom, 0.001);
			Assert::AreEqual(280.0, bottom.picture.top, 0.001);
			Assert::AreEqual(1080.0, bottom.picture.bottom, 0.001);
			for (const auto& fit : { top, center, bottom })
			{
				Assert::AreEqual(0.0, fit.picture.left, 0.001);
				Assert::AreEqual(1920.0, fit.picture.right, 0.001);
				Assert::AreEqual(2.40,
					(fit.picture.right - fit.picture.left) /
					(fit.picture.bottom - fit.picture.top), 0.000001);
				Assert::AreEqual(static_cast<int>(UnusedSpaceAxis::VERTICAL),
					static_cast<int>(fit.unusedAxis));
			}

			// A narrower picture consumes the complete screen height, so vertical
			// alignment cannot introduce movement or change its centered side bars.
			const auto narrowTop = FitAspect(4.0 / 3.0, screen,
				VerticalPictureAlignment::TOP);
			const auto narrowBottom = FitAspect(4.0 / 3.0, screen,
				VerticalPictureAlignment::BOTTOM);
			Assert::AreEqual(narrowTop.picture.top,
				narrowBottom.picture.top, 0.001);
			Assert::AreEqual(narrowTop.picture.bottom,
				narrowBottom.picture.bottom, 0.001);
			Assert::AreEqual(narrowTop.picture.left,
				narrowBottom.picture.left, 0.001);
			Assert::AreEqual(narrowTop.picture.right,
				narrowBottom.picture.right, 0.001);
		}

		TEST_METHOD(ConfiguredScopeScreenHonorsVerticalAlignmentWithinOutput)
		{
			const PresentationRect output = { 0.0, 0.0, 1920.0, 1080.0 };
			const auto top = FitAspect(2.35, output,
				VerticalPictureAlignment::TOP);
			const auto center = FitAspect(2.35, output,
				VerticalPictureAlignment::CENTER);
			const auto bottom = FitAspect(2.35, output,
				VerticalPictureAlignment::BOTTOM);

			const double expectedHeight = 1920.0 / 2.35;
			const double unusedHeight = 1080.0 - expectedHeight;
			Assert::IsTrue(top.valid && center.valid && bottom.valid);
			Assert::AreEqual(0.0, top.picture.top, 0.001);
			Assert::AreEqual(unusedHeight * 0.5,
				center.picture.top, 0.001);
			Assert::AreEqual(unusedHeight, bottom.picture.top, 0.001);
			Assert::AreEqual(1080.0, bottom.picture.bottom, 0.001);
			for (const auto& fit : { top, center, bottom })
			{
				Assert::AreEqual(0.0, fit.picture.left, 0.001);
				Assert::AreEqual(1920.0, fit.picture.right, 0.001);
				Assert::AreEqual(expectedHeight,
					fit.picture.bottom - fit.picture.top, 0.001);
				Assert::AreEqual(static_cast<int>(UnusedSpaceAxis::VERTICAL),
					static_cast<int>(fit.unusedAxis));
			}
		}

		TEST_METHOD(DetectedContentFitsConfiguredScreenWithoutASecondViewport)
		{
			const PresentationRect panel = { 0.0, 0.0, 3840.0, 2160.0 };
			const CenteredFitDecision screen =
				FitCenteredAspect(2.35, panel);
			const CenteredFitDecision narrowerContent =
				FitCenteredAspect(2.20, screen.picture);
			const CenteredFitDecision widerContent =
				FitCenteredAspect(2.40, screen.picture);

			Assert::IsTrue(screen.valid);
			Assert::IsTrue(narrowerContent.valid);
			Assert::IsTrue(widerContent.valid);
			Assert::AreEqual(0.0, screen.picture.left, 0.001);
			Assert::AreEqual(3840.0, screen.picture.right, 0.001);
			Assert::IsTrue(narrowerContent.picture.left > screen.picture.left);
			Assert::IsTrue(narrowerContent.picture.right < screen.picture.right);
			Assert::AreEqual(screen.picture.top,
				narrowerContent.picture.top, 0.001);
			Assert::AreEqual(screen.picture.bottom,
				narrowerContent.picture.bottom, 0.001);
			Assert::AreEqual(screen.picture.left,
				widerContent.picture.left, 0.001);
			Assert::AreEqual(screen.picture.right,
				widerContent.picture.right, 0.001);
			Assert::IsTrue(widerContent.picture.top > screen.picture.top);
			Assert::IsTrue(widerContent.picture.bottom < screen.picture.bottom);
			Assert::AreEqual(static_cast<int>(UnusedSpaceAxis::HORIZONTAL),
				static_cast<int>(narrowerContent.unusedAxis));
			Assert::AreEqual(static_cast<int>(UnusedSpaceAxis::VERTICAL),
				static_cast<int>(widerContent.unusedAxis));
		}

        TEST_METHOD(LoggedBottomPlacementAmbiguityKeepsBothFramesBottomAnchored)
        {
            const int sequences[]={6276,6290};
            const int tops[]={108,96}, bottoms[]={1996,2042};
            for(int i=0;i<2;++i)
            {
                auto crop=TrustedScopeCrop();
                crop.geometry={0,168,3840,1992,3840,2160,3840.0/1824,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                crop.frameSourceSequence=sequences[i];
                crop.latestObservationSupportsCrop=false;
                crop.latestObservationIsProvisional=true;
                crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
                crop.ambiguityHoldActive=true;
                crop.frameLocalPresentationRetentionEvaluated=true;
                crop.frameLocalPresentationRetentionSafe=false;
                crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
                crop.outwardExpansionSourceGeneration=crop.frameSourceGeneration;
                crop.outwardExpansion={0,tops[i],3840,bottoms[i],3840,2160,0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                const auto retained=Evaluate(crop);
                Assert::IsTrue(retained.applyCrop && retained.outwardExpanded);
                Assert::AreEqual(tops[i],retained.sourceBounds.top);
                Assert::AreEqual(bottoms[i],retained.sourceBounds.bottom);
                AspectLimitFillInput input;
                input.trustedContentAuthorityAccepted=retained.applyCrop;
                input.sourceBounds=retained.sourceBounds;
                input.contentReferenceAvailable=true;input.contentReferenceBounds=crop.geometry;
                input.cropWiderContentToFillScreen=input.widerLimitConfigured=true;
                input.widerAspectLimit=2.0;input.screenAspect=16.0/9.0;
                const auto filled=EvaluateAspectLimitFill(input);
                const auto& selected=filled.applied ? filled.sourceBounds : retained.sourceBounds;
                const auto source=ResolveNlsSourceGeometry(retained.applyCrop || filled.applied,
                    selected.left,selected.top,selected.right,selected.bottom,3840,2160);
                Assert::IsTrue(source.valid);
                const auto layout=FitAspect(source.aspect,{0,0,3840,2160},VerticalPictureAlignment::BOTTOM);
                Assert::IsTrue(layout.valid);
                const auto trace="logged sequence="+std::to_string(sequences[i])+" fill="+std::to_string(filled.applied)+
                    " picture.top="+std::to_string(layout.picture.top)+" bottom="+std::to_string(layout.picture.bottom);
                Logger::WriteMessage(trace.c_str());
                Assert::AreEqual(i==0 ? 272.0 : 214.0,layout.picture.top,0.001,
                    L"Ambiguity expansion must not authorize full-height fill of out-of-limit logical content.");
                Assert::AreEqual(2160.0,layout.picture.bottom,0.001);
                Assert::AreEqual(0.0,layout.picture.left,0.001);Assert::AreEqual(3840.0,layout.picture.right,0.001);
                Assert::IsFalse(filled.applied);
                Assert::AreEqual(0,selected.left);Assert::AreEqual(3840,selected.right);
                Assert::AreEqual(tops[i],selected.top);Assert::AreEqual(bottoms[i],selected.bottom);
            }
        }

        TEST_METHOD(LoggedBottomPlacementRefinementDoesNotToggleFullHeightFill)
        {
            // Exact logged snapshots 3906 -> 3908 -> 3911. Legitimate outward
            // protection changes height; bottom alignment does not freeze the top.
            const int sequences[]={3906,3908,3911};
            const int tops[]={94,176,94},bottoms[]={2052,1980,2052};
            for(int i=0;i<3;++i)
            {
                auto crop=TrustedScopeCrop();
                crop.geometry={0,176,3840,1980,3840,2160,3840.0/1804,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                crop.frameSourceSequence=sequences[i];
                crop.latestObservationSupportsCrop=false;
                crop.barCropRefinementPending=true;
                crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.outwardPresentationActive=crop.outwardExpansionAvailable=i!=1;
                crop.outwardExpansionSourceGeneration=crop.frameSourceGeneration;
                crop.outwardExpansion={0,tops[i],3840,bottoms[i],3840,2160,0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                const auto retained=Evaluate(crop);
                Assert::IsTrue(retained.applyCrop);
                Assert::AreEqual(tops[i],retained.sourceBounds.top);Assert::AreEqual(bottoms[i],retained.sourceBounds.bottom);
                AspectLimitFillInput input;
                input.trustedContentAuthorityAccepted=retained.applyCrop;
                input.sourceBounds=retained.sourceBounds;input.contentReferenceAvailable=true;input.contentReferenceBounds=crop.geometry;
                input.cropWiderContentToFillScreen=input.widerLimitConfigured=true;
                input.widerAspectLimit=2.0;input.screenAspect=16.0/9.0;
                const auto filled=EvaluateAspectLimitFill(input);
                const auto& selected=filled.applied ? filled.sourceBounds : retained.sourceBounds;
                const auto source=ResolveNlsSourceGeometry(true,selected.left,selected.top,selected.right,selected.bottom,3840,2160);
                const auto layout=FitAspect(source.aspect,{0,0,3840,2160},VerticalPictureAlignment::BOTTOM);
                Assert::IsTrue(source.valid && layout.valid);
                Logger::WriteMessage(("logged sequence="+std::to_string(sequences[i])+" picture.top="+std::to_string(layout.picture.top)).c_str());
                Assert::AreEqual(i==1 ? 356.0 : 202.0,layout.picture.top,0.001,
                    L"Refinement must not toggle side fill and destroy the bottom-aligned slack.");
                Assert::AreEqual(2160.0,layout.picture.bottom,0.001);
                Assert::AreEqual(0.0,layout.picture.left,0.001);Assert::AreEqual(3840.0,layout.picture.right,0.001);
                Assert::IsFalse(filled.applied);
                Assert::AreEqual(0,selected.left);Assert::AreEqual(3840,selected.right);
            }
        }

        TEST_METHOD(LoggedBottomPlacementEligibleFillStillUsesFullScreen)
        {
            AspectLimitFillInput input;
            input.trustedContentAuthorityAccepted=true;
            input.contentReferenceAvailable=true;
            input.contentReferenceBounds={0,80,3840,2080,3840,2160,1.92,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            input.sourceBounds={0,40,3840,2110,3840,2160,0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            input.cropWiderContentToFillScreen=input.widerLimitConfigured=true;
            input.widerAspectLimit=2.0;input.screenAspect=16.0/9.0;
            const auto filled=EvaluateAspectLimitFill(input);
            Assert::IsTrue(filled.applied);
            const auto& selected=filled.sourceBounds;
            const auto source=ResolveNlsSourceGeometry(true,selected.left,selected.top,selected.right,selected.bottom,3840,2160);
            const auto layout=FitAspect(source.aspect,{0,0,3840,2160},VerticalPictureAlignment::BOTTOM);
            Assert::IsTrue(source.valid && layout.valid);
            Assert::AreEqual(0.0,layout.picture.top,0.001);Assert::AreEqual(2160.0,layout.picture.bottom,0.001);
            Assert::AreEqual(0.0,layout.picture.left,0.001);Assert::AreEqual(3840.0,layout.picture.right,0.001);
        }

        TEST_METHOD(FillContentReferenceRejectsBothLoggedGermanTvZoomBursts)
        {
            for(bool second:{false,true}) {
                AspectLimitFillInput input;
                input.trustedContentAuthorityAccepted=true;
                input.cropWiderContentToFillScreen=input.widerLimitConfigured=true;
                input.widerAspectLimit=2.0;input.screenAspect=16.0/9.0;
                input.contentReferenceAvailable=true;
                input.contentReferenceBounds={0,second?168:176,3840,second?1992:1980,3840,2160,0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                input.sourceBounds={0,second?96:94,3840,second?2042:2052,3840,2160,0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                const auto result=EvaluateAspectLimitFill(input);
                Assert::IsFalse(result.applied,L"An expanded envelope must not bypass a content aspect above the configured 2.0 fill limit.");
                Assert::AreEqual(0,result.sourceBounds.left);Assert::AreEqual(3840,result.sourceBounds.right);
                // The old envelope-only calculation explains the logged side crop.
                input.contentReferenceAvailable=false;
                const auto old=EvaluateAspectLimitFill(input);Assert::IsTrue(old.applied);
                Assert::AreEqual(second?190:180,old.sourceBounds.left);
            }
        }

        TEST_METHOD(FillContentReferenceCannotCrossTheNarrowerLimitOrChangeDirection)
        {
            AspectLimitFillInput input;
            input.trustedContentAuthorityAccepted=true;input.contentReferenceAvailable=true;
            input.cropNarrowerContentToFillScreen=input.narrowerLimitConfigured=true;
            input.narrowerAspectLimit=1.75;input.screenAspect=2.35;
            input.contentReferenceBounds={200,0,3640,2160,3840,2160,0,ActivePictureBounds::BarAxes::LEFT_RIGHT};
            input.sourceBounds={0,0,3840,2160,3840,2160,0,ActivePictureBounds::BarAxes::NONE};
            Assert::IsFalse(EvaluateAspectLimitFill(input).applied);
            input.narrowerLimitConfigured=false;input.cropWiderContentToFillScreen=true;input.screenAspect=1.70;
            // Reference is narrower, expanded envelope wider: no side switch.
            Assert::IsFalse(EvaluateAspectLimitFill(input).applied);
        }

        TEST_METHOD(FillContentReferencePreservesEligibleFillAndFullRaster)
        {
            AspectLimitFillInput input;
            input.trustedContentAuthorityAccepted=true;input.contentReferenceAvailable=true;
            input.cropWiderContentToFillScreen=input.widerLimitConfigured=true;
            input.widerAspectLimit=2.41;input.screenAspect=16.0/9.0;
            input.contentReferenceBounds={0,280,3840,1880,3840,2160,2.4,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            input.sourceBounds={0,200,3840,1960,3840,2160,0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            auto result=EvaluateAspectLimitFill(input);Assert::IsTrue(result.applied);
            Assert::AreEqual(200,result.sourceBounds.top);Assert::AreEqual(1960,result.sourceBounds.bottom);
            input.widerAspectLimit=2.0;Assert::IsFalse(EvaluateAspectLimitFill(input).applied);
            input.cropNarrowerContentToFillScreen=true;input.screenAspect=2.35;
            input.contentReferenceBounds=input.sourceBounds={0,0,3840,2160,3840,2160,16.0/9,ActivePictureBounds::BarAxes::NONE};
            Assert::IsTrue(EvaluateAspectLimitFill(input).applied);
            input.trustedContentAuthorityAccepted=false;Assert::IsFalse(EvaluateAspectLimitFill(input).applied);
        }

        TEST_METHOD(FillContentReferenceRejectsMalformedReferenceAndKeepsActualEnvelopeLimit)
        {
            for(int fault=0;fault<4;++fault) {
                AspectLimitFillInput input;
                input.trustedContentAuthorityAccepted=true;input.contentReferenceAvailable=true;
                input.cropWiderContentToFillScreen=true;input.screenAspect=16.0/9;
                input.sourceBounds=input.contentReferenceBounds={0,280,3840,1880,3840,2160,2.4,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                if(fault==0)input.contentReferenceBounds.bottom=input.contentReferenceBounds.top;
                if(fault==1)input.contentReferenceBounds.left=-2;
                if(fault==2)input.contentReferenceBounds.rasterWidth=1920;
                if(fault==3)input.contentReferenceBounds.bottom=2164;
                Assert::IsFalse(EvaluateAspectLimitFill(input).applied);
            }
            AspectLimitFillInput input;
            input.trustedContentAuthorityAccepted=true;input.contentReferenceAvailable=true;
            input.cropWiderContentToFillScreen=input.widerLimitConfigured=true;
            input.screenAspect=16.0/9;input.widerAspectLimit=2.41;
            input.contentReferenceBounds={176,356,3664,1804,3840,2160,3488.0/1448,ActivePictureBounds::BarAxes::BOTH};
            input.sourceBounds={128,356,3712,1804,3840,2160,3584.0/1448,ActivePictureBounds::BarAxes::BOTH};
            Assert::IsFalse(EvaluateAspectLimitFill(input).applied,L"A nominally eligible picture must not override the actual envelope's existing fill limit.");
        }

        TEST_METHOD(FillOnlyScopeSubtitlesKeepTheCompleteSixteenByNineFrameStationary)
        {
            AspectLimitFillInput fill;
            fill.cropNarrowerContentToFillScreen = fill.cropWiderContentToFillScreen = true;
            fill.narrowerLimitConfigured = fill.widerLimitConfigured = true;
            fill.narrowerAspectLimit = 1.71; fill.widerAspectLimit = 1.86;
            fill.screenAspect = 16.0 / 9.0;
            for (bool subtitleFit : {false, true})
            {
                auto crop = TrustedScopeCrop();
                crop.geometry.top = 276; // Deployed incident: 2.388:1 movie.
                for (int bottom : {1884, 1938, 1958, 2160, 1934, 1884})
                {
                    crop.outwardExpansionAvailable = crop.outwardPresentationActive = bottom > 1884;
                    crop.outwardExpansion = crop.geometry;
                    crop.outwardExpansion.bottom = bottom;
                    crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
                    crop.automaticCropEnabled = ShouldApplyDynamicSourceCrop(false, subtitleFit, crop, fill);
                    crop.verticalTranslationActive = subtitleFit && bottom > 1884;
                    crop.verticalTranslationPixels = 74;
                    PresentationRecoveryInput recovery;
                    recovery.crop = crop;
                    recovery.candidate = Evaluate(crop);
                    recovery.previous.active = true;
                    recovery.previous.trustedCrop = crop.geometry;
                    recovery.previous.sourceGeneration = crop.frameSourceGeneration;
                    const auto recovered = EvaluatePresentationRecovery(recovery);
                    Assert::IsFalse(recovered.state.active);
                    recovery.candidate = recovered.presentation;
                    const auto bounded = EvaluateNearBlackBoundedPresentation({}, recovery);
                    const auto admitted = AdmitCropPresentation({}, crop, bounded.presentation, 1);
                    const auto& result = admitted.presentation;
                    Assert::IsFalse(result.applyCrop);
                    Assert::IsFalse(result.verticallyTranslated);
                    Assert::AreEqual(0, result.sourceBounds.left);
                    Assert::AreEqual(0, result.sourceBounds.top);
                    Assert::AreEqual(3840, result.sourceBounds.right);
                    Assert::AreEqual(2160, result.sourceBounds.bottom);
                    const double aspect = double(result.sourceBounds.right - result.sourceBounds.left) /
                        (result.sourceBounds.bottom - result.sourceBounds.top);
                    const auto fit = FitAspect(aspect, {0, 0, 3840, 2160},
                        VerticalPictureAlignment::CENTER);
                    Assert::AreEqual(0.0, fit.picture.top);
                    Assert::AreEqual(2160.0, fit.picture.bottom);
                }
            }
        }

        TEST_METHOD(FillOnlyEligibleContentStillCropsOnBothSidesOfScreenAspect)
        {
            AspectLimitFillInput fill;
            fill.cropNarrowerContentToFillScreen = fill.cropWiderContentToFillScreen = true;
            fill.narrowerLimitConfigured = fill.widerLimitConfigured = true;
            fill.narrowerAspectLimit = 1.71; fill.widerAspectLimit = 1.86;
            fill.screenAspect = 16.0 / 9.0;
            for (const auto bounds : {
                ActivePictureBounds{0, 44, 3840, 2116, 3840, 2160, 0, ActivePictureBounds::BarAxes::TOP_BOTTOM},
                ActivePictureBounds{40, 0, 3800, 2160, 3840, 2160, 0, ActivePictureBounds::BarAxes::LEFT_RIGHT}})
            {
                auto crop = TrustedScopeCrop(); crop.geometry = bounds;
                crop.automaticCropEnabled = ShouldApplyDynamicSourceCrop(false, false, crop, fill);
                Assert::IsTrue(crop.automaticCropEnabled);
                const auto candidate = Evaluate(crop);
                Assert::IsTrue(candidate.applyCrop);
                fill.sourceBounds = candidate.sourceBounds;
                fill.trustedContentAuthorityAccepted = true;
                Assert::IsTrue(EvaluateAspectLimitFill(fill).applied);
            }
        }

        TEST_METHOD(FillOnlyContentEligibilityRejectsStaleUnknownAndMalformedPicture)
        {
            AspectLimitFillInput fill;
            fill.cropWiderContentToFillScreen = fill.widerLimitConfigured = true;
            fill.widerAspectLimit = 1.86; fill.screenAspect = 16.0 / 9.0;
            for (int fault = 0; fault < 5; ++fault)
            {
                auto crop = TrustedScopeCrop();
                crop.geometry.top = 44; crop.geometry.bottom = 2116;
                if (fault == 0) crop.sharedGeometryAvailable = false;
                if (fault == 1) --crop.geometrySourceGeneration;
                if (fault == 2) crop.classification = ActivePictureClassification::FULL_RASTER_TRUSTED;
                if (fault == 3) crop.geometry.bottom = crop.geometry.top;
                if (fault == 4) crop.geometry.rasterWidth = 1920;
                Assert::IsFalse(ShouldApplyDynamicSourceCrop(false, false, crop, fill));
            }
        }

        TEST_METHOD(FillOnlyEligibilityPreservesExplicitAutoCropAndIndependentSubtitleFit)
        {
            auto crop = TrustedScopeCrop();
            AspectLimitFillInput fill;
            fill.cropWiderContentToFillScreen = fill.widerLimitConfigured = true;
            fill.widerAspectLimit = 1.86; fill.screenAspect = 16.0 / 9.0;
            Assert::IsFalse(ShouldApplyDynamicSourceCrop(false, false, crop, fill));
            Assert::IsTrue(ShouldApplyDynamicSourceCrop(true, false, crop, fill));
            fill.cropWiderContentToFillScreen = false;
            Assert::IsFalse(ShouldApplyDynamicSourceCrop(false, false, crop, fill));
            Assert::IsTrue(ShouldApplyDynamicSourceCrop(false, true, crop, fill));
        }

        TEST_METHOD(FillOnlyUsesContentInsteadOfTransportAspectAndPreservesExactMatches)
        {
            auto crop = TrustedScopeCrop();
            AspectLimitFillInput fill;
            fill.cropNarrowerContentToFillScreen = fill.narrowerLimitConfigured = true;
            fill.narrowerAspectLimit = 2.32; fill.screenAspect = 2.35;
            crop.fullRasterPresentationAuthoritative = true;
            Assert::IsFalse(ShouldApplyDynamicSourceCrop(false, false, crop, fill));
            fill.narrowerAspectLimit = 1.71;
            Assert::IsTrue(ShouldApplyDynamicSourceCrop(false, false, crop, fill));
            crop.fullRasterPresentationAuthoritative = false;
            crop.geometry = {320, 180, 3520, 1980, 3840, 2160, 16.0 / 9.0, ActivePictureBounds::BarAxes::BOTH};
            fill.screenAspect = 16.0 / 9.0;
            Assert::IsTrue(ShouldApplyDynamicSourceCrop(false, false, crop, fill));
        }

		TEST_METHOD(AspectLimitFillCropsTrustedNarrowerAndWiderContent)
		{
			AspectLimitFillInput input;
			input.trustedContentAuthorityAccepted = true;
			input.cropNarrowerContentToFillScreen = true;
			input.narrowerLimitConfigured = true;
			input.narrowerAspectLimit = 2.20;
			input.screenAspect = 2.35;
			input.sourceBounds = {
				0, 208, 3840, 1952, 3840, 2160,
				3840.0 / 1744.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const AspectLimitFillDecision filled =
				EvaluateAspectLimitFill(input);
			Assert::IsTrue(filled.applied);
			Assert::AreEqual(0, filled.sourceBounds.left);
			Assert::AreEqual(3840, filled.sourceBounds.right);
			Assert::IsTrue(filled.sourceBounds.top > input.sourceBounds.top);
			Assert::IsTrue(filled.sourceBounds.bottom < input.sourceBounds.bottom);
			Assert::AreEqual(2.35,
				static_cast<double>(filled.sourceBounds.right - filled.sourceBounds.left) /
				(filled.sourceBounds.bottom - filled.sourceBounds.top), 0.002);

			input.sourceBounds = {
				0, 42, 3840, 2118, 3840, 2160,
				3840.0 / 2076.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const AspectLimitFillDecision narrow =
				EvaluateAspectLimitFill(input);
			Assert::IsFalse(narrow.applied);
			Assert::IsTrue(narrow.reason.find("narrower") != std::string::npos);

			input.trustedContentAuthorityAccepted = false;
			const AspectLimitFillDecision untrusted =
				EvaluateAspectLimitFill(input);
			Assert::IsFalse(untrusted.applied);
			Assert::IsTrue(untrusted.reason.find("trusted") != std::string::npos);

			input.trustedContentAuthorityAccepted = true;
			input.narrowerLimitConfigured = false;
			const AspectLimitFillDecision omittedLimit =
				EvaluateAspectLimitFill(input);
			Assert::IsTrue(omittedLimit.applied);

			// A current trusted full raster is valid content authority too. This
			// is the common no-black-bars case: 16:9 content filling a 2.35:1
			// physical screen by centered top/bottom crop.
			input.sourceBounds = {
				0, 0, 1920, 1080, 1920, 1080,
				16.0 / 9.0, ActivePictureBounds::BarAxes::NONE };
			const AspectLimitFillDecision fullRaster =
				EvaluateAspectLimitFill(input);
			Assert::IsTrue(fullRaster.applied);
			Assert::IsTrue(fullRaster.sourceBounds.top > 0);
			Assert::IsTrue(fullRaster.sourceBounds.bottom < 1080);

			input.cropNarrowerContentToFillScreen = false;
			input.cropWiderContentToFillScreen = true;
			input.widerLimitConfigured = true;
			input.widerAspectLimit = 2.76;
			input.sourceBounds = {
				0, 280, 3840, 1880, 3840, 2160,
				2.40, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const AspectLimitFillDecision wider =
				EvaluateAspectLimitFill(input);
			Assert::IsTrue(wider.applied);
			Assert::IsTrue(wider.sourceBounds.left > input.sourceBounds.left);
			Assert::IsTrue(wider.sourceBounds.right < input.sourceBounds.right);
			Assert::AreEqual(2.35,
				static_cast<double>(wider.sourceBounds.right - wider.sourceBounds.left) /
				(wider.sourceBounds.bottom - wider.sourceBounds.top), 0.002);

			input.widerAspectLimit = 2.35;
			const AspectLimitFillDecision tooWide =
				EvaluateAspectLimitFill(input);
			Assert::IsFalse(tooWide.applied);
			Assert::IsTrue(tooWide.reason.find("wider") != std::string::npos);
		}

		TEST_METHOD(SourceEnvelopeIsIndependentOfScreenAndAnamorphicMapping)
		{
			PresentationEnvelopeGeometryInput source;
			source.trustedPicture = {
				0, 280, 3840, 1880, 3840, 2160, 2.4, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			source.observedContent = source.trustedPicture;
			source.observedContent.bottom = 1908;
			source.observedContentAvailable = true;
			source.expandBottom = true;
			source.verticalPadding = 54;
			const auto envelope = BuildPresentationEnvelope(source);
			Assert::IsTrue(envelope.valid);

			const double sourceAspect = static_cast<double>(
				envelope.bounds.right - envelope.bounds.left) /
				(envelope.bounds.bottom - envelope.bounds.top);
			const double screens[] = { 4.0 / 3.0, 16.0 / 9.0,
				1.85, 2.0, 32.0 / 15.0, 2.35, 2.40 };
			for (double screenAspect : screens)
			{
				const PresentationRect screen = {
					0.0, 0.0, screenAspect * 1000.0, 1000.0 };
				for (double anamorphicScale : { 1.0, 1.25 })
				{
					const auto fit = FitCenteredAspect(
						ApplyAnamorphicLensCompensation(
							sourceAspect, anamorphicScale), screen);
					Assert::IsTrue(fit.valid);
					Assert::AreEqual(280, envelope.bounds.top);
					Assert::AreEqual(1962, envelope.bounds.bottom);
				}
			}
		}

		TEST_METHOD(FixedAspectCropIsIndependentOfThePhysicalScreen)
		{
			FixedAspectCropInput input;
			input.fixedAspect = 2.0;
			input.sourceBounds = {
				0, 0, 3840, 2020, 3840, 2160,
				3840.0 / 2020.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const AspectLimitFillDecision widened = EvaluateFixedAspectCrop(input);
			Assert::IsTrue(widened.applied);
			Assert::AreEqual(2.0,
				static_cast<double>(widened.sourceBounds.right - widened.sourceBounds.left) /
				(widened.sourceBounds.bottom - widened.sourceBounds.top), 0.002);

			input.sourceBounds = {
				0, 0, 3840, 1600, 3840, 2160,
				2.4, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const AspectLimitFillDecision narrowed = EvaluateFixedAspectCrop(input);
			Assert::IsTrue(narrowed.applied);
			Assert::AreEqual(2.0,
				static_cast<double>(narrowed.sourceBounds.right - narrowed.sourceBounds.left) /
				(narrowed.sourceBounds.bottom - narrowed.sourceBounds.top), 0.002);
			Assert::IsTrue(narrowed.sourceBounds.left > input.sourceBounds.left);

			// A detector timeout/fail-open supplies the complete raster. The
			// operator-selected crop remains fixed and cannot fall back to fit.
			input.fixedAspect = 2.35;
			input.sourceBounds = {
				0, 0, 3840, 2160, 3840, 2160,
				16.0 / 9.0, ActivePictureBounds::BarAxes::NONE };
			const AspectLimitFillDecision failOpen = EvaluateFixedAspectCrop(input);
			Assert::IsTrue(failOpen.applied);
			Assert::AreEqual(2.35,
				static_cast<double>(failOpen.sourceBounds.right - failOpen.sourceBounds.left) /
				(failOpen.sourceBounds.bottom - failOpen.sourceBounds.top), 0.002);
		}


        TEST_METHOD(AnamorphicMatchingScreenUsesFullProjectorRaster)
        {
            const auto screen = AnamorphicPresentation::FitScreen(true, 64.0/27.0, 4.0/3.0,
                {0,0,1920,1080}, VerticalPictureAlignment::CENTER, 0);
            const auto picture = AnamorphicPresentation::FitPicture(64.0/27.0, 4.0/3.0,
                screen, VerticalPictureAlignment::CENTER);
            Assert::IsTrue(screen.valid && picture.valid);
            Assert::AreEqual(0.0, picture.picture.left, 1e-8);
            Assert::AreEqual(0.0, picture.picture.top, 1e-8);
            Assert::AreEqual(1920.0, picture.picture.right, 1e-8);
            Assert::AreEqual(1080.0, picture.picture.bottom, 1e-8);
            const auto narrow = AnamorphicPresentation::FitPicture(16.0/9.0, 4.0/3.0,
                screen, VerticalPictureAlignment::CENTER);
            Assert::AreEqual(240.0, narrow.picture.left, 1e-8);
            Assert::AreEqual(0.0, narrow.picture.top, 1e-8);
        }

        TEST_METHOD(AnamorphicPhysicalNlsRatioDoesNotIncludeLens)
        {
            for (double lens : {0.5, 1.0, 16.0/15.0, 1.25, 4.0/3.0, 2.0})
            {
                const auto screen = AnamorphicPresentation::FitScreen(true, 64.0/27.0, lens,
                    {0,0,1920,1080}, VerticalPictureAlignment::CENTER, 0);
                const auto decision = EvaluateNlsMapping(true, 16.0/9.0,
                    screen.physicalAspect, 0.1, 0.0, NlsAspectDirection::ANY, 1.4);
                Assert::AreEqual(static_cast<int>(NlsMappingMode::ACTIVE), static_cast<int>(decision.mode));
                Assert::AreEqual(4.0/3.0, decision.stretchRatio, 1e-8);
                Assert::AreEqual(screen.physicalAspect,
                    (screen.rect.right-screen.rect.left)/(screen.rect.bottom-screen.rect.top)*lens, 1e-8);
            }
        }

        TEST_METHOD(AnamorphicInferredScreenAndExtremeRatiosStayInOutput)
        {
            for (double lens : {0.5, 1.0, 16.0/15.0, 1.25, 4.0/3.0, 2.0})
            {
                const auto inferred = AnamorphicPresentation::FitScreen(false, 0.0, lens,
                    {0,0,1919,1079}, VerticalPictureAlignment::CENTER, 0);
                Assert::AreEqual(1919.0/1079.0*lens, inferred.physicalAspect, 1e-8);
                Assert::AreEqual(0.0, inferred.rect.left, 1e-8);
                Assert::AreEqual(1919.0, inferred.rect.right, 1e-8);
                for (double physical : {1.0, 1.7777777778, 2.35, 4.0})
                    for (double content : {1.0, 1.85, 2.4, 4.0})
                    {
                        const auto screen = AnamorphicPresentation::FitScreen(true, physical, lens,
                            {0,0,1919,1079}, VerticalPictureAlignment::CENTER, 0);
                        const auto fit = AnamorphicPresentation::FitPicture(content, lens, screen,
                            VerticalPictureAlignment::CENTER);
                        Assert::IsTrue(fit.valid);
                        Assert::IsTrue(fit.picture.left >= -1e-6 && fit.picture.top >= -1e-6 &&
                            fit.picture.right <= 1919.000001 && fit.picture.bottom <= 1079.000001);
                        Assert::AreEqual(content, lens*(fit.picture.right-fit.picture.left)/
                            (fit.picture.bottom-fit.picture.top), 1e-8);
                    }
            }
        }

        TEST_METHOD(AnamorphicUnityAndAlignmentPreserveExistingContract)
        {
            const PresentationRect output = {0,0,1920,1080};
            for (auto align : {VerticalPictureAlignment::TOP, VerticalPictureAlignment::CENTER,
                VerticalPictureAlignment::BOTTOM})
            {
                const auto screen = AnamorphicPresentation::FitScreen(true, 2.4, 1.0, output, align, 0);
                const auto oldScreen = FitAspect(2.4, output, align);
                const auto oldFit = FitAspect(1.85, oldScreen.picture, align);
                const auto fit = AnamorphicPresentation::FitPicture(1.85, 1.0, screen, align);
                Assert::AreEqual(oldFit.picture.left, fit.picture.left, 1e-8);
                Assert::AreEqual(oldFit.picture.top, fit.picture.top, 1e-8);
                Assert::AreEqual(oldFit.picture.right, fit.picture.right, 1e-8);
                Assert::AreEqual(oldFit.picture.bottom, fit.picture.bottom, 1e-8);
            }
            const auto top = AnamorphicPresentation::FitScreen(true, 2.4, 1.0, output,
                VerticalPictureAlignment::TOP, 9999);
            Assert::AreEqual(280, top.effectivePadding);
            Assert::AreEqual(1080.0, top.rect.bottom, 1e-8);
            Assert::AreEqual(300.0f, AnamorphicPresentation::OverlayWidth(400.0f, 4.0/3.0), 1e-4f);
            Assert::IsFalse(AnamorphicPresentation::FitScreen(true, 2.4, 0.0, output,
                VerticalPictureAlignment::CENTER, 0).valid);
        }

		TEST_METHOD(TwoToOneLensPrecompressesSixteenByNineToEightByNine)
		{
			const double sourceAspect = 16.0 / 9.0;
			const double compensatedAspect =
				ApplyAnamorphicLensCompensation(sourceAspect, 2.0);

			Assert::AreEqual(8.0 / 9.0, compensatedAspect, 0.000001);
			Assert::AreEqual(sourceAspect,
				ApplyAnamorphicLensCompensation(sourceAspect, 1.0), 0.000001);

			const PresentationRect screen = { 0.0, 0.0, 2350.0, 1000.0 };
			const auto fit = FitCenteredAspect(compensatedAspect, screen);
			Assert::IsTrue(fit.valid);
			Assert::AreEqual(8.0 / 9.0,
				(fit.picture.right - fit.picture.left) /
				(fit.picture.bottom - fit.picture.top), 0.000001);
			Assert::AreEqual(static_cast<int>(UnusedSpaceAxis::HORIZONTAL),
				static_cast<int>(fit.unusedAxis));
		}

		TEST_METHOD(AmbiguityHoldIsBoundedNonRenewableAndGenerationLocal)
		{
			AmbiguityHold hold;
			hold.Observe(900, 7, true, true,
				ActivePictureClassification::BAR_CROP_TRUSTED, 2000);
			hold.Observe(1000, 7, true, false,
				ActivePictureClassification::UNAVAILABLE, 2000);
			Assert::IsTrue(hold.IsActive(2999, 7));

			// More ambiguity cannot renew the original deadline.
			hold.Observe(2500, 7, true, false,
				ActivePictureClassification::PROVISIONAL, 2000);
			Assert::IsFalse(hold.IsActive(3000, 7));
			Assert::IsFalse(hold.IsActive(2999, 8));

			// Current trusted evidence rearms a later, independently bounded fade.
			hold.Observe(3100, 7, true, true,
				ActivePictureClassification::BAR_CROP_TRUSTED, 2000);
			hold.Observe(3200, 7, true, false,
				ActivePictureClassification::UNAVAILABLE, 2000);
			Assert::IsTrue(hold.IsActive(5199, 7));
			hold.Observe(4000, 7, true, false,
				ActivePictureClassification::FULL_RASTER_TRUSTED, 2000);
			Assert::IsFalse(hold.IsActive(4000, 7));

			// A contradiction followed by darkness cannot rearm stale geometry.
			hold.Observe(4100, 7, true, false,
				ActivePictureClassification::UNAVAILABLE, 2000);
			Assert::IsFalse(hold.IsActive(4100, 7));

			// Fresh trusted crop evidence is required before another hold can arm.
			hold.Observe(4200, 7, true, true,
				ActivePictureClassification::BAR_CROP_TRUSTED, 2000);
			hold.Observe(4300, 7, true, false,
				ActivePictureClassification::PROVISIONAL, 2000);
			Assert::IsTrue(hold.IsActive(4300, 7));

			AmbiguityHold inconsistent;
			inconsistent.Observe(5000, 7, true, true,
				ActivePictureClassification::FULL_RASTER_TRUSTED, 2000);
			inconsistent.Observe(5100, 7, true, false,
				ActivePictureClassification::UNAVAILABLE, 2000);
			Assert::IsFalse(inconsistent.IsActive(5100, 7));
		}

		TEST_METHOD(AutomaticCropDefaultsToFailSafeFullRaster)
		{
			Input input = TrustedScopeCrop();
			input.automaticCropEnabled = false;
			const Decision decision = Evaluate(input);
			AssertFullRaster(decision);
			Assert::IsTrue(decision.reason.find("off") != std::string::npos);
		}

		TEST_METHOD(WorldCupFalseCandidatesCannotCropWhileOff)
		{
			const int bars[][2] = {
				{ 258, 1896 }, { 274, 1884 }, { 116, 2054 },
				{ 272, 1886 }, { 272, 1886 }, { 230, 1924 }
			};
			for (const auto& bar : bars)
			{
				Input input = TrustedScopeCrop();
				input.automaticCropEnabled = false;
				input.geometry.top = bar[0];
				input.geometry.bottom = bar[1];
				for (int repeatedObservation = 0;
					repeatedObservation < 100; ++repeatedObservation)
				{
					AssertFullRaster(Evaluate(input));
				}
			}
		}

		TEST_METHOD(ProvisionalGeometryCannotAcquireCropAuthority)
		{
			Input input = TrustedScopeCrop();
			input.classification = ActivePictureClassification::PROVISIONAL;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(StaleGenerationCannotRetainCropAuthority)
		{
			Input input = TrustedScopeCrop();
			input.frameSourceGeneration = 8;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(UnclassifiedLossOfAuthorityWithdrawsToFullRaster)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(AmbiguousDarkObservationRetainsTrustedPresentation)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsUnavailable = true;
			input.ambiguityHoldActive = true;
			const Decision dark = Evaluate(input);
			Assert::IsTrue(dark.applyCrop);
			Assert::AreEqual(274, dark.sourceBounds.top);
			Assert::AreEqual(1884, dark.sourceBounds.bottom);
			Assert::IsTrue(
				dark.reason.find("ambiguity hold") != std::string::npos);

			input.latestObservationIsUnavailable = false;
			input.latestObservationIsProvisional = true;
			const Decision provisional = Evaluate(input);
			Assert::IsTrue(provisional.applyCrop);
			Assert::AreEqual(274, provisional.sourceBounds.top);
			Assert::AreEqual(1884, provisional.sourceBounds.bottom);

			const double finalAspect = static_cast<double>(
				provisional.sourceBounds.right - provisional.sourceBounds.left) /
				(provisional.sourceBounds.bottom - provisional.sourceBounds.top);
			const NlsMappingDecision mapping = EvaluateNlsMapping(
				true, finalAspect, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(NlsMappingMode::LINEAR_PASSTHROUGH),
				static_cast<int>(mapping.mode));
			Assert::AreEqual(finalAspect, mapping.sourceAspect, 0.000001);
		}

		TEST_METHOD(PixelSafeUncertaintyRetainsPresentationWithoutATimer)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = true;
			Decision decision;
			// Duration cannot withdraw a presentation that every current frame
			// positively proves pixel-safe. This models a long dark movie passage.
			for (int frame = 0; frame < 60 * 60; ++frame)
			{
				decision = Evaluate(input);
				Assert::IsTrue(decision.applyCrop);
				Assert::AreEqual(274, decision.sourceBounds.top);
				Assert::AreEqual(1884, decision.sourceBounds.bottom);
			}
			Assert::IsTrue(decision.reason.find("pixel-safe") !=
				std::string::npos);

			input.latestObservationIsProvisional = false;
			input.latestObservationIsUnavailable = true;
			decision = Evaluate(input);
			Assert::IsTrue(decision.applyCrop);
			Assert::AreEqual(274, decision.sourceBounds.top);
			Assert::AreEqual(1884, decision.sourceBounds.bottom);

			input.frameLocalPresentationRetentionSafe = false;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(VisiblePixelsOverrideLegacyAmbiguityTimers)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.sceneVerificationHoldActive = true;
			input.ambiguityHoldActive = true;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(BoundedSceneVerificationRetainsOnlyExistingTrustedCrop)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.sceneVerificationHoldActive = true;
			input.latestObservationIsProvisional = true;
			const Decision decision = Evaluate(input);
			Assert::IsTrue(decision.applyCrop);
			Assert::AreEqual(274, decision.sourceBounds.top);
			Assert::AreEqual(1884, decision.sourceBounds.bottom);
			Assert::IsTrue(
				decision.reason.find("scene verification") != std::string::npos);

			input.latestObservationIsProvisional = false;
			input.latestObservationIsUnavailable = true;
			const Decision darkFade = Evaluate(input);
			Assert::IsTrue(darkFade.applyCrop);
			Assert::AreEqual(274, darkFade.sourceBounds.top);
			Assert::AreEqual(1884, darkFade.sourceBounds.bottom);
		}

		TEST_METHOD(SceneVerificationHoldsTrustedCropAcrossUnreaffirmedBarObservation)
		{
			// VP-0080: the detector can emit a current trusted bar observation
			// whose bounds have not yet settled enough to reaffirm the retained
			// geometry. During the short scene verification hold that must not
			// flash full raster between otherwise identical scope frames.
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.sceneVerificationHoldActive = true;
			input.latestObservationClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			Decision retained;
			// Model the observed alternating current-authority gap. Every frame
			// must retain the same known scope window rather than pulse full raster.
			for (int frame = 0; frame < 100; ++frame)
			{
				input.latestObservationSupportsCrop = (frame & 1) == 0;
				retained = Evaluate(input);
				Assert::IsTrue(retained.applyCrop);
				Assert::AreEqual(274, retained.sourceBounds.top);
				Assert::AreEqual(1884, retained.sourceBounds.bottom);
			}
			Assert::IsTrue(retained.reason.find("scene verification") !=
				std::string::npos);

			// A frame-local visible-pixel conflict remains authoritative even in
			// the same verification window.
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			AssertFullRaster(Evaluate(input));

			// A current trusted full-raster observation also withdraws immediately.
			input.frameLocalPresentationRetentionEvaluated = false;
			input.latestObservationClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(SceneVerificationCannotOverrideFullRasterOrStaleAuthority)
		{
			Input unavailable = TrustedScopeCrop();
			unavailable.latestObservationSupportsCrop = false;
			unavailable.sceneVerificationHoldActive = true;
			AssertFullRaster(Evaluate(unavailable));

			Input fullRaster = TrustedScopeCrop();
			fullRaster.latestObservationSupportsCrop = false;
			fullRaster.sceneVerificationHoldActive = true;
			fullRaster.latestObservationIsProvisional = true;
			fullRaster.classification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			AssertFullRaster(Evaluate(fullRaster));

			Input stale = TrustedScopeCrop();
			stale.latestObservationSupportsCrop = false;
			stale.sceneVerificationHoldActive = true;
			stale.latestObservationIsProvisional = true;
			stale.frameSourceGeneration = 8;
			AssertFullRaster(Evaluate(stale));
		}

		TEST_METHOD(SubtitleReleaseSettlesAtTrustedBaseWithoutAFullRasterFlash)
		{
			// The terminal zero-shift drift sample must still present its exact
			// generation-current base while the next detector observation arrives.
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.verticalTranslationBaseRetentionActive = true;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration =
				input.frameSourceGeneration;
			const Decision settled = Evaluate(input);
			Assert::IsTrue(settled.applyCrop);
			Assert::IsFalse(settled.verticallyTranslated);
			Assert::AreEqual(274, settled.sourceBounds.top);
			Assert::AreEqual(1884, settled.sourceBounds.bottom);
			Assert::IsTrue(settled.reason.find("release settled") !=
				std::string::npos);

			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			AssertFullRaster(Evaluate(input));

			input.frameLocalPresentationRetentionEvaluated = false;
			input.verticalTranslationSourceGeneration = 8;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(RendererReleaseTimelinePreservesAspectAndReturnsMonotonically)
		{
			VerticalTranslationDrift drift;
			Assert::AreEqual(0.0f,
				drift.Resolve(94.0f, 1000, 1000), 0.001f);
			Assert::AreEqual(94.0f,
				drift.Resolve(94.0f, 2000, 1000), 0.001f);

			const uint64_t releaseTicks[] = { 3000, 3250, 3500, 3750 };
			const int expectedShifts[] = { 94, 72, 48, 24 };
			int previousTop = 2160;
			for (size_t index = 0; index < _countof(releaseTicks); ++index)
			{
				const float driftShift = drift.Resolve(
					0.0f, releaseTicks[index], 1000);
				VerticalBarPresentationResolutionInput resolutionInput;
				resolutionInput.detailedAction =
					VerticalBarPresentationAction::TRANSLATE;
				resolutionInput.translationPixels = driftShift;
				resolutionInput.authoritativeTop = 276;
				resolutionInput.authoritativeBottom = 1884;
				resolutionInput.rasterHeight = 2160;
				const auto routing = ResolveVerticalBarRendererRouting(
					ResolveVerticalBarPresentation(resolutionInput));
				Assert::IsTrue(routing.translationActive);
				Assert::IsFalse(routing.fitActive);
				Assert::AreEqual(expectedShifts[index],
					routing.translationPixels);

				Input crop = TrustedScopeCrop();
				crop.geometry = {
					0, 276, 3840, 1884, 3840, 2160,
					3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
				crop.verticalTranslationActive = routing.translationActive;
				crop.verticalTranslationPixels = routing.translationPixels;
				crop.verticalTranslationBase = crop.geometry;
				crop.verticalTranslationSourceGeneration =
					crop.frameSourceGeneration;
				const Decision selected = Evaluate(crop);
				Assert::IsTrue(selected.verticallyTranslated);
				Assert::IsFalse(selected.outwardExpanded);
				Assert::AreEqual(1608, selected.sourceBounds.bottom -
					selected.sourceBounds.top);
				Assert::AreEqual(expectedShifts[index],
					selected.sourceBounds.top - 276);
				Assert::IsTrue(selected.sourceBounds.top <= previousTop);
				previousTop = selected.sourceBounds.top;
				const NlsSourceGeometry nls = ResolveNlsSourceGeometry(
					selected.applyCrop, selected.sourceBounds.left,
					selected.sourceBounds.top, selected.sourceBounds.right,
					selected.sourceBounds.bottom, 3840, 2160);
				Assert::IsTrue(nls.valid);
				Assert::AreEqual(3840.0 / 1608.0, nls.aspect, 0.000001);
			}

			Assert::AreEqual(0.0f,
				drift.Resolve(0.0f, 4000, 1000), 0.001f);
			Assert::IsFalse(drift.IsActive());
			Input settled = TrustedScopeCrop();
			settled.geometry = {
				0, 276, 3840, 1884, 3840, 2160,
				3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			settled.latestObservationSupportsCrop = false;
			settled.verticalTranslationBaseRetentionActive =
				drift.ConsumeFinalBaseFrame();
			settled.verticalTranslationBase = settled.geometry;
			settled.verticalTranslationSourceGeneration =
				settled.frameSourceGeneration;
			const Decision finalBase = Evaluate(settled);
			Assert::IsTrue(finalBase.applyCrop);
			Assert::IsFalse(finalBase.verticallyTranslated);
			Assert::AreEqual(276, finalBase.sourceBounds.top);
			Assert::AreEqual(1884, finalBase.sourceBounds.bottom);
			Assert::AreEqual(1608, finalBase.sourceBounds.bottom -
				finalBase.sourceBounds.top);
		}

        struct CaptionConfirmationHoldFixture
        {
            CaptionConfirmationPresentationHoldInput input;
            explicit CaptionConfirmationHoldFixture(bool upper = false)
            {
                input.sourceGeneration = 2; input.analysisDomainKey = 7;
                input.sourceFormatKey = 11; input.presentationEpoch = 3;
                input.sourceSequence = 1210; input.currentTick = 1000;
                input.picture = {0,268,3840,1892,3840,2160,2.3645,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                input.retentionBase = input.picture;
                input.caption = {1246,upper ? 116 : 1778,2606,upper ? 382 : 2042,
                    3840,2160,0.0,ActivePictureBounds::BarAxes::NONE};
                input.visibleBounds = input.picture;
                if (upper) input.visibleBounds.top = 128;
                else input.visibleBounds.bottom = 2024;
                input.lastRendered.available = true;
                input.lastRendered.bounds = {0,62,3840,2106,3840,2160,0.0,ActivePictureBounds::BarAxes::NONE};
                input.lastRendered.sourceGeneration = input.sourceGeneration;
                input.lastRendered.analysisDomainKey = input.analysisDomainKey;
                input.lastRendered.sourceFormatKey = input.sourceFormatKey;
                input.lastRendered.presentationEpoch = input.presentationEpoch;
                input.lastRendered.sourceSequence = 1209; input.lastRendered.tick = 958;
                input.eligible = input.pictureCurrentTrusted = input.captionCurrentMeasured = true;
                input.nearBlackEvaluated = input.pendingConfirmation = true;
                input.targetTranslationPixels = upper ? -174.0 : 174.0;
                PairEvidence();
            }
            void PairEvidence()
            {
                input.pictureGeneration = input.captionGeneration = input.visibleGeneration =
                    input.retentionGeneration = input.sourceGeneration;
                input.pictureSequence = input.captionSequence = input.visibleSequence =
                    input.retentionSequence = input.sourceSequence;
            }
            void Commit(const CaptionConfirmationPresentationHoldDecision& decision)
            {
                Assert::IsTrue(decision.hold);
                input.previous = decision.state;
                input.lastRendered.bounds = decision.bounds;
                input.lastRendered.sourceSequence = input.sourceSequence;
                input.lastRendered.tick = input.currentTick;
            }
            void Advance()
            {
                ++input.sourceSequence; input.currentTick += 42; input.cadenceRepeat = false;
                PairEvidence();
            }
        };

        TEST_METHOD(CaptionConfirmationKeepsExactRenderedAnchorThroughPendingAndZeroEngage)
        {
            for (bool upper : {false, true})
            {
                CaptionConfirmationHoldFixture fixture(upper);
                const auto originalAnchor = fixture.input.lastRendered;
                for (int frame = 0; frame < 6; ++frame)
                {
                    fixture.input.pendingConfirmation = frame < 5;
                    fixture.input.zeroEngage = frame == 5;
                    const auto held = EvaluateCaptionConfirmationPresentationHold(fixture.input);
                    Assert::IsTrue(held.hold);
                    Assert::AreEqual(62, held.bounds.top);
                    Assert::AreEqual(2106, held.bounds.bottom);
                    Assert::AreEqual(uint64_t(1000), held.state.startedTick);
                    Assert::AreEqual(uint64_t(1210), held.state.firstSequence);
                    Assert::AreEqual(originalAnchor.sourceSequence, held.state.anchor.sourceSequence);
                    fixture.Commit(held);
                    // Repeated rendering can reuse this exact source proof but
                    // cannot refresh the absolute deadline or anchor identity.
                    fixture.input.cadenceRepeat = true;
                    ++fixture.input.currentTick;
                    const auto repeated = EvaluateCaptionConfirmationPresentationHold(fixture.input);
                    Assert::IsTrue(repeated.hold);
                    Assert::AreEqual(held.state.startedTick, repeated.state.startedTick);
                    Assert::AreEqual(held.state.firstSequence, repeated.state.firstSequence);
                    fixture.Commit(repeated);
                    fixture.Advance();
                }
                fixture.input.pendingConfirmation = fixture.input.zeroEngage = false;
                const auto released = EvaluateCaptionConfirmationPresentationHold(fixture.input);
                Assert::IsFalse(released.hold);
                Assert::IsFalse(released.state.active);
            }
        }

        TEST_METHOD(OrderedPendingCaptionCadenceHoldsUntilAtomicTranslationWithoutChangingVotes)
        {
            for (bool upper : {false, true})
            {
                const float direction = upper ? -1.0f : 1.0f;
                CaptionConfirmationHoldFixture fixture(upper);
                VerticalTranslationConfirmationState confirmations;
                VerticalBarPresentationState owner;
                VerticalTranslationDrift drift;
                CropPresentationAdmissionState admission;
                admission.available = true;
                admission.sourceGeneration = fixture.input.sourceGeneration;
                admission.presentationEpoch = fixture.input.presentationEpoch;
                admission.trustedCrop = fixture.input.picture;
                admission.trustedCrop.top = 68; admission.trustedCrop.bottom = 2092;
                PresentationRecoveryState recoveryState;
                recoveryState.active = true;
                recoveryState.sourceGeneration = fixture.input.sourceGeneration;
                recoveryState.presentationEpoch = fixture.input.presentationEpoch;
                recoveryState.trustedCrop = fixture.input.picture;
                recoveryState.startedSourceSequence = 964;
                recoveryState.startedTick = 700;
                recoveryState.lastSourceSequence = 1209;
                // Exercise a coherent already-withdrawn recovery episode. The
                // recorded cadence/rectangles are inputs, not a reconstruction
                // of every earlier native measurement in the live recording.
                recoveryState.fallbackBoundsAvailable = true;
                recoveryState.fallbackBounds = {0, 0, 3840, 2160, 3840, 2160,
                    3840.0 / 2160.0, ActivePictureBounds::BarAxes::NONE};
                unsigned analyzedSamples = 0;
                for (uint64_t sequence = 1210; sequence <= 1216; ++sequence)
                {
                    Assert::AreEqual(sequence, fixture.input.sourceSequence);
                    const uint64_t tick = fixture.input.currentTick;
                    if (sequence == 1210 || sequence == 1212 || sequence == 1215)
                    {
                        VerticalTranslationConfirmationInput scan;
                        scan.previous = confirmations;
                        scan.observed.action = VerticalBarPresentationAction::TRANSLATE;
                        scan.observed.translationPixels = direction * 164.0f;
                        scan.sourceSequence = sequence;
                        scan.targetBufferPixels = 10;
                        scan.maximumTranslationMagnitudePixels = 268;
                        const auto confirmed = ConfirmVerticalTranslation(scan);
                        confirmations = confirmed.state;
                        ++analyzedSamples;
                        Assert::AreEqual(sequence == 1215, confirmed.newlyAccepted);
                        if (confirmed.newlyAccepted)
                        {
                            owner.action = confirmed.effective.action;
                            owner.translationPixels = confirmed.effective.translationPixels;
                            owner.sourceSequence = sequence;
                            owner.lastDetectionTick = tick;
                            Assert::AreEqual(direction * 174.0f, owner.translationPixels);
                        }
                    }
                    const unsigned expectedVotes = sequence < 1212 ? 1u : sequence < 1215 ? 2u : 0u;
                    Assert::AreEqual(expectedVotes, unsigned(confirmations.confirmations));
                    const auto votesBeforeHold = confirmations;
                    const float pose = drift.Resolve(owner.translationPixels, tick, 500);
                    VerticalBarPresentationResolutionInput resolution;
                    resolution.detailedAction = owner.action;
                    resolution.translationPixels = pose;
                    resolution.zeroTranslationRetainsTrustedBase = drift.IsActive();
                    resolution.authoritativeTop = fixture.input.picture.top;
                    resolution.authoritativeBottom = fixture.input.picture.bottom;
                    resolution.rasterHeight = 2160;
                    const auto routing = ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(resolution));

                    Input crop;
                    auto acquisition = MakeCaptionFitInput(crop, 3840, 2160, 268, 1892,
                        1246, upper ? 116 : 1778, 2606, upper ? 382 : 2042);
                    crop.frameSourceGeneration = crop.geometrySourceGeneration = fixture.input.sourceGeneration;
                    crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                    crop.frameLocalPresentationRetentionEvaluated = true;
                    crop.frameSourceSequence = crop.currentVisibleSourceSequence = sequence;
                    crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
                    crop.currentVisibleBounds = fixture.input.visibleBounds;
                    crop.verticalTranslationBase = crop.geometry;
                    crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
                    crop.verticalTranslationPixels = routing.translationPixels;
                    crop.verticalTranslationActive = routing.translationActive;
                    crop.verticalTranslationConfirmationPending = confirmations.confirmations != 0;
                    crop.verticalTranslationEngageBaseRetentionActive = drift.IsActive() && !routing.translationActive;
                    acquisition.captionSourceGeneration = crop.frameSourceGeneration;
                    acquisition.captionSourceSequence = sequence;
                    acquisition.captionConfirmed = sequence > 1210; // First measured cue is one observation.
                    acquisition.translationProposed = routing.translationActive;
                    acquisition.initialAcquisition = !HasCurrentCropPresentationAdmission(admission, crop, 3);
                    const auto original = crop;
                    const auto proof = BuildCaptionBoundedFitAcquisition(acquisition);
                    Assert::AreEqual(sequence == 1216, proof.available);
                    if (proof.available)
                    {
                        crop.verticalTranslationActive = false;
                        crop.verticalTranslationEngageBaseRetentionActive = false;
                        crop.outwardPresentationActive = crop.outwardExpansionAvailable = true;
                        crop.outwardExpansion = proof.bounds;
                        crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
                    }
                    PresentationRecoveryInput recovery;
                    recovery.previous = recoveryState;
                    recovery.previousAdmission = admission;
                    recovery.crop = crop;
                    recovery.candidate = Evaluate(crop);
                    recovery.measurementCurrent = recovery.retentionEvaluated = recovery.observationAvailable = true;
                    recovery.retentionSourceGeneration = crop.frameSourceGeneration;
                    recovery.retentionSourceSequence = sequence;
                    recovery.retentionBounds = recovery.observation = recovery.observedTrustedCrop = crop.geometry;
                    recovery.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                    recovery.nearBlackEvaluated = true;
                    recovery.currentTick = tick;
                    recovery.presentationEpoch = 3;
                    recovery.framesPerSecond = 23.976;
                    recovery.confirmedPresentationResolved = proof.available;
                    if(proof.available)QualifyExplicitCaptionFitFixture(recovery,acquisition.captionBounds);
                    const auto recovered = EvaluatePresentationRecovery(recovery);
                    recoveryState = recovered.state;
                    recovery.previous = recoveryState;
                    recovery.candidate = recovered.presentation;
                    const auto bounded = EvaluateNearBlackBoundedPresentation({}, recovery);
                    const auto admitted = AdmitCropPresentation(admission, crop, bounded.presentation, 3);

                    fixture.input.pendingConfirmation = confirmations.confirmations != 0;
                    fixture.input.zeroEngage = sequence == 1215 && drift.IsActive() && !routing.translationActive;
                    fixture.input.eligible = !admitted.presentation.applyCrop;
                    const auto beforeHold = recoveryState;
                    const auto held = EvaluateCaptionConfirmationPresentationHold(fixture.input);
                    Assert::AreEqual(votesBeforeHold.confirmations, confirmations.confirmations);
                    Assert::AreEqual(votesBeforeHold.lastObservedSourceSequence, confirmations.lastObservedSourceSequence);
                    Assert::AreEqual(votesBeforeHold.candidateTranslationPixels, confirmations.candidateTranslationPixels);
                    Assert::AreEqual(beforeHold.startedTick, recoveryState.startedTick);
                    Assert::AreEqual(beforeHold.startedSourceSequence, recoveryState.startedSourceSequence);
                    Assert::AreEqual(beforeHold.samples, recoveryState.samples);
                    ActivePictureBounds finalBounds;
                    if (sequence < 1216)
                    {
                        Assert::IsFalse(admitted.presentation.applyCrop);
                        Assert::IsTrue(recoveryState.active);
                        Assert::AreEqual(uint64_t(700), recoveryState.startedTick);
                        Assert::AreEqual(uint64_t(964), recoveryState.startedSourceSequence);
                        Assert::IsTrue(held.hold);
                        finalBounds = held.bounds;
                        Assert::AreEqual(62, finalBounds.top);
                        Assert::AreEqual(2106, finalBounds.bottom);
                        fixture.Commit(held);
                        // The bridge has not admitted the new logical scope.
                        Assert::AreEqual(68, admission.trustedCrop.top);
                        Assert::AreEqual(2092, admission.trustedCrop.bottom);
                    }
                    else
                    {
                        Assert::IsFalse(held.hold);
                        Assert::IsFalse(recoveryState.active);
                        Assert::IsTrue(admitted.presentation.owner == DecisionOwner::OUTWARD_FIT);
                        EstablishedSubtitleTranslationInput translation;
                        translation.base = original.geometry;
                        translation.sourceGeneration = original.frameSourceGeneration;
                        translation.presentation = owner;
                        translation.holdMs = 2000;
                        translation.resolvedTranslationPixels = routing.translationPixels;
                        translation.engagingTranslation = drift.IsActive();
                        translation.engageIntervalAvailable = drift.TryGetActiveInterval(
                            translation.engageStartTranslationPixels, translation.engageTargetTranslationPixels,
                            translation.engageAppliedTranslationPixels);
                        acquisition.crop = &original;
                        const auto final = CompleteCaptionAcquisitionTranslation(acquisition, admitted, translation, 3, tick);
                        Assert::IsTrue(final.presentation.owner == DecisionOwner::VERTICAL_TRANSLATION);
                        Assert::IsTrue(final.presentation.applyCrop);
                        finalBounds = final.presentation.sourceBounds;
                        Assert::AreEqual(1624, finalBounds.bottom - finalBounds.top);
                        Assert::AreEqual(268, final.state.trustedCrop.top);
                    }
                    Assert::IsTrue(finalBounds.top > 0 && finalBounds.bottom < 2160);
                    fixture.Advance();
                }
                Assert::AreEqual(3u, analyzedSamples);
            }
        }

        TEST_METHOD(CaptionConfirmationHoldRejectsUnpairedUnsafeOrUnrenderedEvidence)
        {
            for (int variant = 0; variant < 32; ++variant)
            {
                CaptionConfirmationHoldFixture fixture;
                auto& i = fixture.input;
                switch (variant)
                {
                case 0: i.eligible = false; break;
                case 1: i.competingPresentation = true; break;
                case 2: i.pictureCurrentTrusted = false; break;
                case 3: i.captionCurrentMeasured = false; break;
                case 4: i.nearBlackEvaluated = false; break;
                case 5: i.nearBlack = true; break;
                case 6: --i.pictureSequence; break;
                case 7: --i.captionSequence; break;
                case 8: --i.visibleSequence; break;
                case 9: --i.retentionSequence; break;
                case 10: ++i.captionGeneration; break;
                case 11: ++i.visibleGeneration; break;
                case 12: ++i.retentionGeneration; break;
                case 13: i.retentionBase.top += 2; break;
                case 14: i.lastRendered.available = false; break;
                case 15: ++i.lastRendered.sourceGeneration; break;
                case 16: ++i.lastRendered.analysisDomainKey; break;
                case 17: ++i.lastRendered.sourceFormatKey; break;
                case 18: ++i.lastRendered.presentationEpoch; break;
                case 19: --i.lastRendered.sourceSequence; break;
                case 20: i.lastRendered.tick = 1001; break;
                case 21: i.lastRendered.tick = 1; break;
                case 22: i.lastRendered.bounds.bottom = 2040; break;
                case 23: i.lastRendered.bounds.left = 2; break;
                case 24: i.visibleBounds.top = 200; break;
                case 25: i.visibleBounds.bottom = 2046; break;
                case 26: i.caption.top = 100; break;
                case 27: i.targetTranslationPixels = -174; break;
                case 28: i.targetTranslationPixels = std::numeric_limits<double>::infinity(); break;
                case 29: i.cadenceRepeat = true; break;
                case 30: i.picture.trustedBarAxes = ActivePictureBounds::BarAxes::NONE; break;
                case 31: ++i.pictureGeneration; break;
                }
                const auto rejected = EvaluateCaptionConfirmationPresentationHold(i);
                Assert::IsFalse(rejected.hold, (L"hold rejection " + std::to_wstring(variant)).c_str());
                Assert::IsFalse(rejected.state.active);
            }
        }

        TEST_METHOD(CaptionConfirmationHoldCannotRenewAcrossGapsTargetChangesOrDeadline)
        {
            for (int variant = 0; variant < 8; ++variant)
            {
                CaptionConfirmationHoldFixture fixture;
                fixture.Commit(EvaluateCaptionConfirmationPresentationHold(fixture.input));
                fixture.Advance();
                auto& i = fixture.input;
                switch (variant)
                {
                case 0: ++i.sourceSequence; fixture.PairEvidence(); break;
                case 1: i.targetTranslationPixels += 2; break;
                case 2: i.picture.top += 2; i.retentionBase = i.picture; i.visibleBounds.top = i.picture.top; break;
                case 3: ++i.sourceFormatKey; i.lastRendered.sourceFormatKey = i.sourceFormatKey; break;
                case 4: ++i.analysisDomainKey; i.lastRendered.analysisDomainKey = i.analysisDomainKey; break;
                case 5: ++i.presentationEpoch; i.lastRendered.presentationEpoch = i.presentationEpoch; break;
                case 6: i.currentTick = 1500; i.lastRendered.tick = 1499; break;
                case 7: i.lastRendered.bounds.top -= 2; break;
                }
                const auto rejected = EvaluateCaptionConfirmationPresentationHold(i);
                Assert::IsFalse(rejected.hold, (L"hold continuity rejection " + std::to_wstring(variant)).c_str());
                Assert::IsFalse(rejected.state.active);
            }
            CaptionConfirmationHoldFixture repeated;
            repeated.Commit(EvaluateCaptionConfirmationPresentationHold(repeated.input));
            repeated.input.cadenceRepeat = true;
            repeated.input.currentTick = 1499;
            repeated.input.lastRendered.tick = 1498;
            const auto last = EvaluateCaptionConfirmationPresentationHold(repeated.input);
            Assert::IsTrue(last.hold);
            repeated.Commit(last);
            repeated.input.currentTick = 1500;
            Assert::IsFalse(EvaluateCaptionConfirmationPresentationHold(repeated.input).hold);
        }

        TEST_METHOD(NewScopeContractAcquiresCaptionFitAfterTallPictureWasAdmitted)
        {
            Input crop;
            auto request = MakeCaptionFitInput(crop,3840,2160,268,1892,1246,1778,2606,2042);
            CropPresentationAdmissionState previous;
            previous.available=true;previous.sourceGeneration=crop.frameSourceGeneration;
            previous.presentationEpoch=3;previous.trustedCrop=crop.geometry;
            previous.trustedCrop.top=68;previous.trustedCrop.bottom=2092;
            // The former available-only check suppressed the safe fit here.
            request.initialAcquisition=!HasCurrentCropPresentationAdmission(previous,crop,3);
            Assert::IsTrue(request.initialAcquisition);
            const auto bounded=BuildCaptionBoundedFitAcquisition(request);
            Assert::IsTrue(bounded.available);
            Assert::AreEqual(268,bounded.bounds.top);
            Assert::AreEqual(2044,bounded.bounds.bottom);
            crop.outwardPresentationActive=true;crop.outwardExpansionAvailable=true;
            crop.outwardExpansion=bounded.bounds;crop.outwardExpansionSourceGeneration=crop.frameSourceGeneration;
            const auto admitted=AdmitCropPresentation(previous,crop,Evaluate(crop),3);
            Assert::IsFalse(admitted.blocked);
            Assert::IsTrue(admitted.presentation.applyCrop);
            Assert::IsTrue(HasCurrentCropPresentationAdmission(admitted.state,crop,3));
            request.initialAcquisition=!HasCurrentCropPresentationAdmission(admitted.state,crop,3);
            Assert::IsFalse(request.initialAcquisition);
            Assert::IsFalse(BuildCaptionBoundedFitAcquisition(request).available);
        }

        TEST_METHOD(CaptionAcquisitionAdmissionIdentityCannotCrossEpochGenerationOrGeometry)
        {
            Input crop;
            auto request=MakeCaptionFitInput(crop,3840,2160,268,1892,1246,1778,2606,2042);
            CropPresentationAdmissionState previous;
            previous.available=true;previous.sourceGeneration=crop.frameSourceGeneration;
            previous.presentationEpoch=3;previous.trustedCrop=crop.geometry;
            Assert::IsTrue(HasCurrentCropPresentationAdmission(previous,crop,3));
            for(int mismatch=0;mismatch<6;++mismatch) {
                auto changed=previous;
                if(mismatch==0) changed.sourceGeneration++;
                if(mismatch==1) changed.presentationEpoch++;
                if(mismatch==2) changed.trustedCrop.rasterHeight++;
                if(mismatch==3) changed.trustedCrop.top-=2;
                if(mismatch==4) changed.trustedCrop.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
                if(mismatch==5) changed.available=false;
                request.initialAcquisition=!HasCurrentCropPresentationAdmission(changed,crop,3);
                Assert::IsTrue(request.initialAcquisition);
                // A stale admission never supplies missing current authority.
                request.captionConfirmed=false;
                Assert::IsFalse(BuildCaptionBoundedFitAcquisition(request).available);
                request.captionConfirmed=true;
                crop.latestObservationSupportsCrop=false;
                Assert::IsFalse(BuildCaptionBoundedFitAcquisition(request).available);
                crop.latestObservationSupportsCrop=true;
            }
            crop.currentVisibleBounds.top=100; // Picture outside the caption union remains a veto.
            Assert::IsFalse(BuildCaptionBoundedFitAcquisition(request).available);
        }

        TEST_METHOD(CaptionAcquisitionPublishesFinalTranslationWithoutIntermediateFit)
        {
            for (bool top : {false, true})
            {
                Input crop;
                auto request = MakeCaptionFitInput(crop, 3840, 2160, 268, 1892,
                    1246, top ? 116 : 1778, 2606, top ? 382 : 2042);
                const auto proof = BuildCaptionBoundedFitAcquisition(request);
                auto fit = crop;
                fit.outwardPresentationActive = fit.outwardExpansionAvailable = true;
                fit.outwardExpansion = proof.bounds;
                fit.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
                const auto admitted = AdmitCropPresentation({}, fit, Evaluate(fit), 3);
                Assert::IsFalse(admitted.blocked);
                EstablishedSubtitleTranslationInput owner;
                owner.base = crop.geometry;
                owner.sourceGeneration = crop.frameSourceGeneration;
                owner.presentation.action = VerticalBarPresentationAction::TRANSLATE;
                owner.presentation.sourceSequence = crop.frameSourceSequence - 1;
                owner.presentation.lastDetectionTick = 990;
                owner.holdMs = 5000;
                owner.presentation.translationPixels = top ? -174.0f : 174.0f;
                owner.resolvedTranslationPixels = top ? -34 : 34;
                owner.engagingTranslation = owner.engageIntervalAvailable = true;
                owner.engageTargetTranslationPixels = owner.presentation.translationPixels;
                owner.engageAppliedTranslationPixels = float(owner.resolvedTranslationPixels);
                crop.verticalTranslationActive = true;
                crop.verticalTranslationBase = crop.geometry;
                crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
                crop.verticalTranslationPixels = owner.resolvedTranslationPixels;
                const auto final = CompleteCaptionAcquisitionTranslation(request, admitted, owner, 3, 1000);
                Assert::IsTrue(final.presentation.owner == DecisionOwner::VERTICAL_TRANSLATION);
                Assert::IsTrue(final.presentation.verticallyTranslated);
                Assert::IsFalse(final.presentation.outwardExpanded);
                Assert::IsFalse(final.state.outwardPresentationAvailable);
                Assert::AreEqual(1624, final.presentation.sourceBounds.bottom - final.presentation.sourceBounds.top);
                // The next frame uses the same final size, with only normal drift.
                ++crop.frameSourceSequence;
                ++crop.currentVisibleSourceSequence;
                crop.verticalTranslationPixels += top ? -2 : 2;
                const auto next = AdmitCropPresentation(final.state, crop, Evaluate(crop), 3);
                Assert::IsFalse(next.blocked);
                Assert::AreEqual(1624, next.presentation.sourceBounds.bottom - next.presentation.sourceBounds.top);
            }
        }

        TEST_METHOD(RecordedCaptionAcquisitionRunsRecoveryAdmissionRoutingAndHandoffInRendererOrder)
        {
            for (int direction : {-1, 1})
            for (int variant = 0; variant < 4; ++variant)
            {
                Input original;
                auto request = MakeCaptionFitInput(original, 3840, 2160, 268, 1892,
                    1246, direction < 0 ? 116 : 1778, 2606, direction < 0 ? 382 : 2042);
                original.frameSourceSequence = original.currentVisibleSourceSequence = 842;
                request.captionSourceSequence = 842;
                VerticalTranslationDrift drift;
                drift.Resolve(0, 900, 0);
                drift.Resolve(174.0f * direction, 1000, 500);
                const float pose = drift.Resolve(174.0f * direction, variant == 3 ? 1048 : 1050, 500);
                VerticalBarPresentationResolutionInput resolution;
                resolution.detailedAction = VerticalBarPresentationAction::TRANSLATE;
                resolution.translationPixels = pose;
                resolution.authoritativeTop = original.geometry.top;
                resolution.authoritativeBottom = original.geometry.bottom;
                resolution.rasterHeight = original.rasterHeight;
                const auto routing = ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(resolution));
                if (variant == 3) Assert::IsTrue(std::abs(pose - routing.translationPixels) > 1.0f);
                Assert::AreEqual(18 * direction, routing.translationPixels);
                original.verticalTranslationActive = routing.translationActive;
                original.verticalTranslationPixels = routing.translationPixels;
                original.verticalTranslationBase = original.geometry;
                original.verticalTranslationSourceGeneration = original.frameSourceGeneration;
                EstablishedSubtitleTranslationInput owner;
                owner.base = original.geometry;
                owner.sourceGeneration = original.frameSourceGeneration;
                owner.presentation.action = VerticalBarPresentationAction::TRANSLATE;
                owner.presentation.sourceSequence = 841;
                owner.presentation.lastDetectionTick = 1000;
                owner.presentation.translationPixels = 174.0f * direction;
                owner.holdMs = 2000;
                owner.resolvedTranslationPixels = routing.translationPixels;
                owner.engagingTranslation = drift.IsActive();
                owner.engageIntervalAvailable = drift.TryGetActiveInterval(
                    owner.engageStartTranslationPixels, owner.engageTargetTranslationPixels,
                    owner.engageAppliedTranslationPixels);
                CropPresentationAdmissionState prior;
                prior.available = true;
                prior.sourceGeneration = original.frameSourceGeneration;
                prior.presentationEpoch = 1;
                prior.trustedCrop = original.geometry;
                prior.trustedCrop.top = 68;
                prior.trustedCrop.bottom = 2092;
                request.initialAcquisition = !HasCurrentCropPresentationAdmission(prior, original, 1);
                auto fit = original;
                fit.verticalTranslationActive = false;
                fit.outwardPresentationActive = fit.outwardExpansionAvailable = true;
                fit.outwardExpansion = BuildCaptionBoundedFitAcquisition(request).bounds;
                fit.outwardExpansionSourceGeneration = fit.frameSourceGeneration;
                PresentationRecoveryInput recovery;
                recovery.crop = fit;
                recovery.previousAdmission = prior;
                recovery.previous.active = true;
                recovery.previous.sourceGeneration = fit.frameSourceGeneration;
                recovery.previous.presentationEpoch = 1;
                recovery.previous.trustedCrop = fit.geometry;
                recovery.previous.startedSourceSequence = 836;
                recovery.previous.startedTick = 800;
                recovery.previous.lastSourceSequence = 841;
                recovery.currentTick = 1050;
                recovery.presentationEpoch = 1;
                recovery.measurementCurrent = recovery.retentionEvaluated = true;
                recovery.retentionSourceGeneration = fit.frameSourceGeneration;
                recovery.retentionSourceSequence = 842;
                recovery.retentionBounds = fit.geometry;
                recovery.confirmedPresentationResolved = true;
                recovery.candidate = Evaluate(fit);
                QualifyExplicitCaptionFitFixture(recovery,request.captionBounds);
                const auto recovered = EvaluatePresentationRecovery(recovery);
                Assert::IsFalse(recovered.state.active);
                recovery.previous = recovered.state;
                recovery.candidate = recovered.presentation;
                const auto bounded = EvaluateNearBlackBoundedPresentation({}, recovery);
                const auto admitted = AdmitCropPresentation(prior, fit, bounded.presentation, 1);
                Assert::IsFalse(admitted.blocked);
                Assert::IsTrue(admitted.presentation.owner == DecisionOwner::OUTWARD_FIT);
                // Mirror final renderer input restoration, preserving later vetoes.
                auto translated = fit;
                translated.verticalTranslationActive = original.verticalTranslationActive;
                translated.outwardPresentationActive = original.outwardPresentationActive;
                translated.outwardExpansionAvailable = original.outwardExpansionAvailable;
                translated.outwardExpansion = original.outwardExpansion;
                translated.outwardExpansionSourceGeneration = original.outwardExpansionSourceGeneration;
                if (variant == 1) translated.verticalInspectionPending = true;
                if (variant == 2) owner.competingPresentation = true;
                request.crop = &translated;
                std::string reason;
                const auto final = CompleteCaptionAcquisitionTranslation(request, admitted, owner, 1, 1050, &reason);
                Assert::AreEqual(std::string((variant == 0 || variant == 3) ? "accepted" :
                    variant == 1 ? "inspection-pending" : "competing-presentation"), reason);
                Assert::IsTrue(final.presentation.owner == ((variant == 0 || variant == 3) ?
                    DecisionOwner::VERTICAL_TRANSLATION : DecisionOwner::OUTWARD_FIT));
            }
        }

        TEST_METHOD(CaptionAtomicHandoffRejectsStaleOwnersAndLateVetoesWithoutChangingSafeFit)
        {
            for (int variant = 0; variant < 27; ++variant)
            {
                Input crop;
                auto request = MakeCaptionFitInput(crop, 3840, 2160, 268, 1892, 1246, 1778, 2606, 2042);
                auto fit = crop;
                fit.outwardPresentationActive = fit.outwardExpansionAvailable = true;
                fit.outwardExpansion = BuildCaptionBoundedFitAcquisition(request).bounds;
                fit.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
                auto admitted = AdmitCropPresentation({}, fit, Evaluate(fit), 3);
                EstablishedSubtitleTranslationInput owner;
                owner.base = crop.geometry;
                owner.sourceGeneration = crop.frameSourceGeneration;
                owner.presentation.action = VerticalBarPresentationAction::TRANSLATE;
                owner.presentation.sourceSequence = crop.frameSourceSequence - 1;
                owner.presentation.lastDetectionTick = 990;
                owner.holdMs = 5000;
                owner.presentation.translationPixels = 174.0f;
                owner.resolvedTranslationPixels = 34;
                owner.engagingTranslation = owner.engageIntervalAvailable = true;
                owner.engageTargetTranslationPixels = 174.0f;
                owner.engageAppliedTranslationPixels = 34.0f;
                crop.verticalTranslationActive = true;
                crop.verticalTranslationBase = crop.geometry;
                crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
                crop.verticalTranslationPixels = 34;
                switch (variant)
                {
                case 0: owner.presentation.sourceSequence = crop.frameSourceSequence + 1; break;
                case 1: ++owner.sourceGeneration; break;
                case 2: owner.presentation.translationPixels = -174; break;
                case 3: owner.presentation.translationPixels = 100; break;
                case 4: owner.engageIntervalAvailable = false; break;
                case 5: owner.engageAppliedTranslationPixels = 50; break;
                case 6: owner.competingPresentation = true; break;
                case 7: crop.verticalTranslationConfirmationPending = true; break;
                case 8: crop.verticalInspectionPending = true; break;
                case 9: crop.presentationFailOpen = true; break;
                case 10: crop.nearBlackEpisodeFullRaster = true; break;
                case 11: crop.currentVisibleBounds.top = 100; break;
                case 12: --request.captionSourceSequence; break;
                case 13: --admitted.state.presentationSourceSequence; break;
                case 14: ++admitted.state.presentationEpoch; break;
                case 15: admitted.blocked = true; break;
                case 16: crop.latestObservationSupportsCrop = false; break;
                case 17: crop.verticalTranslationSourceGeneration++; break;
                case 18: owner.holdMs = 1; break;
                case 19: owner.engageTargetTranslationPixels = 150; break;
                case 20: owner.engageStartTranslationPixels = 300; break;
                case 21: owner.engageAppliedTranslationPixels = 200; break;
                case 22: admitted.presentation.applyCrop = false; break;
                case 23: crop.nearBlackEpisodeRetainCrop = true; break;
                case 24: crop.verticalFitConfirmationPending = true; break;
                case 25: owner.holdMs = 0; owner.presentation.sourceSequence = 0; break;
                case 26: owner.holdMs = 0; owner.presentation.sourceSequence = crop.frameSourceSequence + 1; break;
                }
                const auto rejected = CompleteCaptionAcquisitionTranslation(request, admitted, owner, 3, 1000);
                Assert::AreEqual(admitted.blocked, rejected.blocked);
                Assert::IsTrue(rejected.presentation.owner == admitted.presentation.owner,
                    (L"rejection variant " + std::to_wstring(variant)).c_str());
                Assert::AreEqual(admitted.presentation.reason, rejected.presentation.reason);
                Assert::AreEqual(admitted.presentation.sourceBounds.top, rejected.presentation.sourceBounds.top);
                Assert::AreEqual(admitted.presentation.sourceBounds.bottom, rejected.presentation.sourceBounds.bottom);
                Assert::AreEqual(admitted.state.presentationSourceSequence, rejected.state.presentationSourceSequence);
                Assert::AreEqual(admitted.state.outwardPresentationAvailable, rejected.state.outwardPresentationAvailable);
            }
        }

		TEST_METHOD(ConfirmedCaptionFitAcquiresOnlyTheCurrentPictureAndCaptionEnvelope)
		{
			Input crop;
			auto request = MakeCaptionFitInput(crop, 3840, 2160,
				276, 1884, 1414, 1670, 2414, 1914);
			crop.verticalTranslationActive = true;
			crop.verticalTranslationPixels = 72;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration = crop.frameSourceGeneration;

			const auto translated = Evaluate(crop);
			Assert::IsTrue(translated.applyCrop);
			Assert::IsTrue(translated.verticallyTranslated);
			Assert::AreEqual(348, translated.sourceBounds.top);
			Assert::AreEqual(1956, translated.sourceBounds.bottom);

			// The old same-height translation clips the trusted picture's top
			// 72 rows. The existing current-visible veto must continue to reject it.
			const auto blocked = AdmitCropPresentation({}, crop, translated, 1);
			Assert::IsTrue(blocked.blocked);
			AssertFullRaster(blocked.presentation);
			Assert::IsFalse(blocked.state.available);

			const auto bounded = BuildCaptionBoundedFitAcquisition(request);
			Assert::IsTrue(bounded.available);
			Assert::AreEqual(0, bounded.bounds.left);
			Assert::AreEqual(276, bounded.bounds.top);
			Assert::AreEqual(3840, bounded.bounds.right);
			Assert::AreEqual(1916, bounded.bounds.bottom);

			// Route the caption-bounded candidate through the ordinary policy
			// and admission stages, exactly as the renderer does after the helper.
			crop.verticalTranslationActive = false;
			crop.verticalTranslationPixels = 0;
			crop.outwardPresentationActive = true;
			crop.outwardExpansionAvailable = true;
			crop.outwardExpansion = bounded.bounds;
			crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
			const auto fit = Evaluate(crop);
			Assert::IsTrue(fit.applyCrop);
			Assert::IsTrue(fit.outwardExpanded);
			Assert::IsFalse(fit.verticallyTranslated);
			Assert::AreEqual(static_cast<int>(DecisionOwner::OUTWARD_FIT),
				static_cast<int>(fit.owner));
			Assert::AreEqual(276, fit.sourceBounds.top);
			Assert::AreEqual(1916, fit.sourceBounds.bottom);

			PresentationRecoveryInput recovery;
			recovery.crop = crop;
			recovery.candidate = fit;
			recovery.presentationEpoch = 1;
			const auto recovered = EvaluatePresentationRecovery(recovery);
			Assert::IsFalse(recovered.state.active);
			Assert::AreEqual(276, recovered.presentation.sourceBounds.top);
			Assert::AreEqual(1916, recovered.presentation.sourceBounds.bottom);

			const auto admitted = AdmitCropPresentation({}, crop,
				recovered.presentation, 1);
			Assert::IsFalse(admitted.blocked);
			Assert::IsTrue(admitted.state.available);
			Assert::AreEqual(276, admitted.presentation.sourceBounds.top);
			Assert::AreEqual(1916, admitted.presentation.sourceBounds.bottom);

			for (const auto mode : { SubtitlePreviewMode::None,
				SubtitlePreviewMode::GeneratedGrayBackground,
				SubtitlePreviewMode::BlackBackground })
			{
				const auto display = SubtitlePresentationBounds(fit, mode, true, false);
				Assert::AreEqual(fit.sourceBounds.top, display.top);
				Assert::AreEqual(fit.sourceBounds.bottom, display.bottom);
				Assert::AreEqual(fit.sourceBounds.bottom - fit.sourceBounds.top,
					display.bottom - display.top);
			}
		}

		TEST_METHOD(ConfirmedCaptionFitUsesMeasuredCoordinatesAtTopBottomAndSmallRaster)
		{
			struct Fixture
			{
				int width, height, pictureTop, pictureBottom;
				int captionLeft, captionTop, captionRight, captionBottom;
				int expectedTop, expectedBottom;
			};
			const Fixture fixtures[] = {
				{ 3840, 2160, 276, 1884, 1414, 1670, 2414, 1914, 276, 1916 },
				{ 3840, 2160, 276, 1884, 1414, 246, 2414, 520, 244, 1884 },
				{ 640, 360, 46, 314, 232, 300, 410, 338, 46, 340 },
			};
			for (const auto& fixture : fixtures)
			{
				Input crop;
				const auto request = MakeCaptionFitInput(crop,
					fixture.width, fixture.height,
					fixture.pictureTop, fixture.pictureBottom,
					fixture.captionLeft, fixture.captionTop,
					fixture.captionRight, fixture.captionBottom);
				const auto bounded = BuildCaptionBoundedFitAcquisition(request);
				Assert::IsTrue(bounded.available);
				Assert::AreEqual(fixture.expectedTop, bounded.bounds.top);
				Assert::AreEqual(fixture.expectedBottom, bounded.bounds.bottom);
				Assert::AreEqual(fixture.width, bounded.bounds.right);
				Assert::AreEqual(fixture.width, bounded.bounds.rasterWidth);
				Assert::AreEqual(fixture.height, bounded.bounds.rasterHeight);
				Assert::AreEqual(
					static_cast<double>(fixture.width) /
						(fixture.expectedBottom - fixture.expectedTop),
					bounded.bounds.aspectRatio, 0.000001);
			}
		}

		TEST_METHOD(CaptionFitRejectsStaleUnconfirmedUnpairedOrWrongEdgeEvidence)
		{
			for (int fault = 0; fault < 14; ++fault)
			{
				Input crop;
				auto request = MakeCaptionFitInput(crop, 3840, 2160,
					276, 1884, 1414, 1670, 2414, 1914);
				switch (fault)
				{
				case 0: crop.sharedGeometryAvailable = false; break; // Missing picture authority.
				case 1: request.captionConfirmed = false; break; // Broad texture is not a confirmed glyph/card.
				case 2: --request.captionSourceGeneration; break;
				case 3: --request.captionSourceSequence; break;
				case 4: crop.currentVisibleBoundsAvailable = false; break;
				case 5: --crop.currentVisibleSourceGeneration; break;
				case 6: --crop.currentVisibleSourceSequence; break;
				case 7: crop.currentVisibleBase.top += 2; break;
				case 8: // A box crossing both trusted picture edges has no single caption side.
					request.captionBounds.top = 250;
					request.captionBounds.bottom = 1914;
					break;
				case 9: // A box contained inside the picture cannot justify an outward fit.
					request.captionBounds.top = 1670;
					request.captionBounds.bottom = 1800;
					break;
				case 10: // Current unrelated content extends beyond the bounded caption proof.
					crop.currentVisibleBounds.bottom = 1924;
					break;
				case 11: request.initialAcquisition = false; break;
				case 12: request.translationProposed = false; break;
				case 13: // The caption lies beyond the current raster.
					request.captionBounds.bottom = 2162;
					break;
				}
				const auto bounded = BuildCaptionBoundedFitAcquisition(request);
				Assert::IsFalse(bounded.available,
					L"Caption extent cannot replace stale picture or visible-pixel proof.");
			}

			Input crop;
			auto broad = MakeCaptionFitInput(crop, 3840, 2160,
				276, 1884, 0, 1600, 3840, 1940);
			broad.captionConfirmed = false;
			const auto broadTexture = BuildCaptionBoundedFitAcquisition(broad);
			Assert::IsFalse(broadTexture.available);
		}

		TEST_METHOD(CaptionFitDoesNotReplaceAConfirmedRealPictureExpansion)
		{
			Input crop;
			auto request = MakeCaptionFitInput(crop, 3840, 2160,
				276, 1884, 1414, 1670, 2414, 1914);
			crop.outwardPresentationActive = true;
			crop.outwardExpansionAvailable = true;
			crop.outwardExpansionSourceGeneration = crop.frameSourceGeneration;
			crop.outwardExpansion = crop.geometry;
			crop.outwardExpansion.bottom = 2000;
			crop.outwardExpansion.aspectRatio = 3840.0 / 1724.0;
			crop.currentVisibleBounds.bottom = 2000;
			crop.currentVisibleBounds.aspectRatio = 3840.0 / 1724.0;

			Assert::IsFalse(BuildCaptionBoundedFitAcquisition(request).available);
			const auto pictureExpansion = Evaluate(crop);
			Assert::IsTrue(pictureExpansion.applyCrop);
			Assert::IsTrue(pictureExpansion.outwardExpanded);
			Assert::AreEqual(static_cast<int>(DecisionOwner::OUTWARD_FIT),
				static_cast<int>(pictureExpansion.owner));
			Assert::AreEqual(276, pictureExpansion.sourceBounds.top);
			Assert::AreEqual(2000, pictureExpansion.sourceBounds.bottom);
			const auto admitted = AdmitCropPresentation({}, crop,
				pictureExpansion, 1);
			Assert::IsFalse(admitted.blocked);
			Assert::AreEqual(2000, admitted.presentation.sourceBounds.bottom);
		}

		TEST_METHOD(SparseSubtitleTranslatesSameSizeWindowWithoutChangingNlsAspect)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.verticalTranslationActive = true;
			input.verticalTranslationPixels = 75;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration = 7;
			const Decision decision = Evaluate(input);
			Assert::IsTrue(decision.applyCrop);
			Assert::IsFalse(decision.outwardExpanded);
			Assert::IsTrue(decision.verticallyTranslated);
			Assert::AreEqual(76, decision.verticalTranslationPixels);
			Assert::AreEqual(350, decision.sourceBounds.top);
			Assert::AreEqual(1960, decision.sourceBounds.bottom);
			Assert::AreEqual(1610, decision.sourceBounds.bottom -
				decision.sourceBounds.top);

			const double finalAspect = static_cast<double>(
				decision.sourceBounds.right - decision.sourceBounds.left) /
				(decision.sourceBounds.bottom - decision.sourceBounds.top);
			const NlsMappingDecision mapping = EvaluateNlsMapping(
				true, finalAspect, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(NlsMappingMode::LINEAR_PASSTHROUGH),
				static_cast<int>(mapping.mode));
			Assert::AreEqual(finalAspect, mapping.sourceAspect, 0.000001);
			Assert::AreEqual(3840.0 / 1610.0, finalAspect, 0.000001);
		}

		TEST_METHOD(SparseTopSubtitleRoundsAwayFromZeroAndClampsBothDirections)
		{
			Input top = TrustedScopeCrop();
			top.verticalTranslationActive = true;
			top.verticalTranslationPixels = -75;
			top.verticalTranslationBase = top.geometry;
			top.verticalTranslationSourceGeneration = 7;
			Decision decision = Evaluate(top);
			Assert::AreEqual(-76, decision.verticalTranslationPixels);
			Assert::AreEqual(198, decision.sourceBounds.top);
			Assert::AreEqual(1808, decision.sourceBounds.bottom);

			top.verticalTranslationPixels = -1000;
			decision = Evaluate(top);
			Assert::AreEqual(-274, decision.verticalTranslationPixels);
			Assert::AreEqual(0, decision.sourceBounds.top);
			Assert::AreEqual(1610, decision.sourceBounds.bottom);

			Input bottom = TrustedScopeCrop();
			bottom.verticalTranslationActive = true;
			bottom.verticalTranslationPixels = 1000;
			bottom.verticalTranslationBase = bottom.geometry;
			bottom.verticalTranslationSourceGeneration = 7;
			decision = Evaluate(bottom);
			Assert::AreEqual(276, decision.verticalTranslationPixels);
			Assert::AreEqual(550, decision.sourceBounds.top);
			Assert::AreEqual(2160, decision.sourceBounds.bottom);
		}

		TEST_METHOD(SparseTranslationRequiresExactCurrentBaseAndGeneration)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.verticalTranslationActive = true;
			input.verticalTranslationPixels = 75;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration = 7;

			Input stale = input;
			stale.verticalTranslationSourceGeneration = 6;
			AssertFullRaster(Evaluate(stale));

			Input changed = input;
			changed.geometry.top += 2;
			changed.geometry.bottom += 2;
			AssertFullRaster(Evaluate(changed));
		}

		TEST_METHOD(HorizontalFitAndVerticalTranslationComposeWithoutChangingHeight)
		{
			VerticalBarPresentationResolution translation;
			translation.action = VerticalBarPresentationAction::TRANSLATE;
			translation.translationPixels = 75.0f;
			const auto routing = ResolveVerticalBarRendererRouting(translation);
			Assert::IsTrue(routing.translationActive);
			Assert::IsFalse(routing.fitActive);

			Input input = TrustedScopeCrop();
			input.geometry = {
				200, 274, 3640, 1884, 3840, 2160,
				3440.0 / 1610.0, ActivePictureBounds::BarAxes::BOTH };
			input.outwardPresentationActive = true;
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			input.outwardExpansion.left = 100;
			input.outwardExpansion.right = 3700;
			input.outwardExpansion.aspectRatio = 3600.0 / 1610.0;
			input.outwardExpansionSourceGeneration = 7;
			input.verticalTranslationActive = routing.translationActive;
			input.verticalTranslationPixels = routing.translationPixels;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration = 7;

			const Decision decision = Evaluate(input);
			Assert::IsTrue(decision.outwardExpanded);
			Assert::IsTrue(decision.verticallyTranslated);
			Assert::AreEqual(100, decision.sourceBounds.left);
			Assert::AreEqual(3700, decision.sourceBounds.right);
			Assert::AreEqual(350, decision.sourceBounds.top);
			Assert::AreEqual(1960, decision.sourceBounds.bottom);
			Assert::AreEqual(1610, decision.sourceBounds.bottom -
				decision.sourceBounds.top);
		}

		TEST_METHOD(VerticalFitCannotComposeWithVerticalTranslation)
		{
			Input input = TrustedScopeCrop();
			input.outwardPresentationActive = true;
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			input.outwardExpansion.top = 100;
			input.outwardExpansion.aspectRatio = 3840.0 /
				(input.outwardExpansion.bottom - input.outwardExpansion.top);
			input.outwardExpansionSourceGeneration = 7;
			input.verticalTranslationActive = true;
			input.verticalTranslationPixels = -75;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration = 7;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(FullRasterAuthorityOverridesHeldTranslation)
		{
			Input input = TrustedScopeCrop();
			input.fullRasterPresentationAuthoritative = true;
			input.verticalTranslationActive = true;
			input.verticalTranslationPixels = 75;
			input.verticalTranslationBase = input.geometry;
			input.verticalTranslationSourceGeneration = 7;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(BarContentFitExpandsOnlyTheDetectedEdges)
		{
			Input input = TrustedScopeCrop();
			input.outwardPresentationActive = true;
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			// A top-bar intrusion must not manufacture a matching bottom expansion
			// merely because the original source is CIH.
			input.outwardExpansion.top = 96;
			input.outwardExpansion.aspectRatio = 3840.0 / (1884 - 96);
			input.outwardExpansionSourceGeneration = 7;
			Decision decision = Evaluate(input);
			Assert::IsTrue(decision.outwardExpanded);
			Assert::AreEqual(96, decision.sourceBounds.top);
			Assert::AreEqual(1884, decision.sourceBounds.bottom);

			// When both old bars carry genuine picture, preserve both extents.
			input.outwardExpansion.bottom = 2064;
			input.outwardExpansion.aspectRatio = 3840.0 / (2064 - 96);
			decision = Evaluate(input);
			Assert::IsTrue(decision.outwardExpanded);
			Assert::AreEqual(96, decision.sourceBounds.top);
			Assert::AreEqual(2064, decision.sourceBounds.bottom);
		}

		TEST_METHOD(OutwardPresentationSupportsAllFourEdgesAndFullRaster)
		{
			Input full = TrustedScopeCrop();
			full.outwardPresentationActive = true;
			full.outwardExpansionAvailable = true;
			full.outwardExpansion = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, ActivePictureBounds::BarAxes::NONE };
			full.outwardExpansionSourceGeneration = 7;
			const Decision fullDecision = Evaluate(full);
			Assert::IsTrue(fullDecision.outwardExpanded);
			Assert::AreEqual(0, fullDecision.sourceBounds.top);
			Assert::AreEqual(2160, fullDecision.sourceBounds.bottom);

			Input pillar = TrustedScopeCrop();
			pillar.geometry = {
				480, 0, 3360, 2160, 3840, 2160, 4.0 / 3.0, ActivePictureBounds::BarAxes::LEFT_RIGHT };
			pillar.outwardPresentationActive = true;
			pillar.outwardExpansionAvailable = true;
			pillar.outwardExpansion = {
				120, 0, 3700, 2160, 3840, 2160,
				3580.0 / 2160.0, ActivePictureBounds::BarAxes::NONE };
			pillar.outwardExpansionSourceGeneration = 7;
			const Decision pillarDecision = Evaluate(pillar);
			Assert::IsTrue(pillarDecision.outwardExpanded);
			Assert::AreEqual(120, pillarDecision.sourceBounds.left);
			Assert::AreEqual(3700, pillarDecision.sourceBounds.right);
		}

		TEST_METHOD(ProvisionalOverlayWithinSceneHoldMayExpandExistingAuthority)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.latestObservationIsProvisional = true;
			input.sceneVerificationHoldActive = true;
			input.outwardPresentationActive = true;
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			input.outwardExpansion.top = 100;
			input.outwardExpansion.aspectRatio = 3840.0 / (1884 - 100);
			input.outwardExpansionSourceGeneration = 7;
			const Decision decision = Evaluate(input);
			Assert::IsTrue(decision.applyCrop);
			Assert::IsTrue(decision.outwardExpanded);
			Assert::AreEqual(100, decision.sourceBounds.top);
			Assert::AreEqual(1884, decision.sourceBounds.bottom);
		}

		TEST_METHOD(OverlayExpansionFailsSafeWhenMissingStaleOrInward)
		{
			Input missing = TrustedScopeCrop();
			missing.outwardPresentationActive = true;
			AssertFullRaster(Evaluate(missing));

			Input stale = TrustedScopeCrop();
			stale.outwardPresentationActive = true;
			stale.outwardExpansionAvailable = true;
			stale.outwardExpansion = stale.geometry;
			stale.outwardExpansion.bottom = 2100;
			stale.outwardExpansionSourceGeneration = 6;
			AssertFullRaster(Evaluate(stale));

			Input inward = TrustedScopeCrop();
			inward.outwardPresentationActive = true;
			inward.outwardExpansionAvailable = true;
			inward.outwardExpansion = inward.geometry;
			inward.outwardExpansion.top = 300;
			inward.outwardExpansionSourceGeneration = 7;
			AssertFullRaster(Evaluate(inward));
		}

		TEST_METHOD(CurrentOutwardEnvelopeIsSafeWithoutNewCropAuthority)
		{
			Input input = TrustedScopeCrop();
			input.latestObservationSupportsCrop = false;
			input.outwardPresentationActive = true;
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			input.outwardExpansion.bottom = 2100;
			input.outwardExpansionSourceGeneration = 7;
			Assert::IsTrue(Evaluate(input).outwardExpanded);

			input.latestObservationIsProvisional = true;
			Assert::IsTrue(Evaluate(input).outwardExpanded);
		}

		TEST_METHOD(PillarboxAuthorityCanDriveHorizontalPresentationFit)
		{
			Input input = TrustedScopeCrop();
			input.geometry = {
				480, 0, 3360, 2160, 3840, 2160, 4.0 / 3.0, ActivePictureBounds::BarAxes::LEFT_RIGHT };
			input.outwardPresentationActive = true;
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			input.outwardExpansion.left = 400;
			input.outwardExpansionSourceGeneration = 7;
			const Decision decision = Evaluate(input);
			Assert::IsTrue(decision.outwardExpanded);
			Assert::AreEqual(400, decision.sourceBounds.left);
		}

		TEST_METHOD(ProfileEpochRequiresFreshOverlayEvidence)
		{
			Input overlaid = TrustedScopeCrop();
			overlaid.outwardPresentationActive = true;
			overlaid.outwardExpansionAvailable = true;
			overlaid.outwardExpansion = overlaid.geometry;
			overlaid.outwardExpansion.bottom = 2100;
			overlaid.outwardExpansionSourceGeneration = 7;
			Assert::IsTrue(Evaluate(overlaid).outwardExpanded);

			Input profileEpoch = overlaid;
			profileEpoch.sharedGeometryAvailable = false;
			AssertFullRaster(Evaluate(profileEpoch));

			const Decision reacquired = Evaluate(TrustedScopeCrop());
			Assert::IsTrue(reacquired.applyCrop);
			Assert::IsFalse(reacquired.outwardExpanded);
			Assert::AreEqual(274, reacquired.sourceBounds.top);
			Assert::AreEqual(1884, reacquired.sourceBounds.bottom);
		}

		TEST_METHOD(OverlayReleaseReturnsDirectlyToTrustedCrop)
		{
			Input input = TrustedScopeCrop();
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			input.outwardExpansion.bottom = 2100;
			input.outwardExpansionSourceGeneration = 7;
			const Decision decision = Evaluate(input);
			Assert::IsTrue(decision.applyCrop);
			Assert::IsFalse(decision.outwardExpanded);
			Assert::AreEqual(274, decision.sourceBounds.top);
			Assert::AreEqual(1884, decision.sourceBounds.bottom);
		}

		TEST_METHOD(BoundsWithoutAxisAuthorityCannotAcquireCropAuthority)
		{
			Input input = TrustedScopeCrop();
			input.geometry.trustedBarAxes =
				ActivePictureBounds::BarAxes::NONE;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(WrongAxisAuthorityCannotAcquireCropAuthority)
		{
			Input input = TrustedScopeCrop();
			input.geometry.trustedBarAxes =
				ActivePictureBounds::BarAxes::LEFT_RIGHT;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(RasterMismatchCannotCrop)
		{
			Input input = TrustedScopeCrop();
			input.geometry.rasterHeight = 1080;
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(InvalidAndMisalignedBoundsCannotCrop)
		{
			Input invalid = TrustedScopeCrop();
			invalid.geometry.bottom = 2162;
			AssertFullRaster(Evaluate(invalid));

			Input misaligned = TrustedScopeCrop();
			misaligned.geometry.top = 273;
			AssertFullRaster(Evaluate(misaligned));
		}

		TEST_METHOD(TrustedFullRasterDoesNotBecomeACrop)
		{
			Input input = TrustedScopeCrop();
			input.classification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			input.geometry = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, ActivePictureBounds::BarAxes::NONE };
			AssertFullRaster(Evaluate(input));
		}

		TEST_METHOD(CurrentSharedTrustedCropIsTheOnlyAcceptedCrop)
		{
			const Decision decision = Evaluate(TrustedScopeCrop());
			Assert::IsTrue(decision.applyCrop);
			Assert::AreEqual(274, decision.sourceBounds.top);
			Assert::AreEqual(1884, decision.sourceBounds.bottom);
			Assert::IsTrue(decision.reason.find("accepted") != std::string::npos);
		}

		TEST_METHOD(SceneBoundaryKeepsMatchingBarAndFullRasterPresentation)
		{
			SceneInput bar;
			bar.geometryAvailable = true;
			bar.geometryIsCurrentGeneration = true;
			bar.latestEvidenceIsCurrent = true;
			bar.latestObservationSupportsCrop = true;
			bar.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			bar.latestClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::KEEP_CURRENT),
				static_cast<int>(EvaluateSceneBoundary(bar).action));

			SceneInput full = bar;
			full.latestObservationSupportsCrop = false;
			full.geometryClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			full.latestClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::KEEP_CURRENT),
				static_cast<int>(EvaluateSceneBoundary(full).action));
		}

		TEST_METHOD(SceneBoundaryHoldsOnlyExistingTrustedScopeSnapshot)
		{
			SceneInput provisional;
			provisional.geometryAvailable = true;
			provisional.geometryIsCurrentGeneration = true;
			provisional.latestEvidenceIsCurrent = true;
			provisional.existingCropCanBeSnapshotted = true;
			provisional.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			provisional.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::HOLD_SNAPSHOT),
				static_cast<int>(EvaluateSceneBoundary(provisional).action));

			SceneInput darkFade = provisional;
			darkFade.latestClassification =
				ActivePictureClassification::UNAVAILABLE;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::HOLD_SNAPSHOT),
				static_cast<int>(EvaluateSceneBoundary(darkFade).action));

			provisional.existingCropCanBeSnapshotted = false;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::PRESERVE_REFERENCE),
				static_cast<int>(EvaluateSceneBoundary(provisional).action));

			darkFade.existingCropCanBeSnapshotted = true;
			darkFade.geometryClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::WITHDRAW),
				static_cast<int>(EvaluateSceneBoundary(darkFade).action));
		}

		TEST_METHOD(PixelSafeCutKeepsPresentationWithoutSnapshotExpiry)
		{
			SceneInput input;
			input.geometryAvailable = true;
			input.geometryIsCurrentGeneration = true;
			input.latestEvidenceIsCurrent = true;
			input.existingCropCanBeSnapshotted = true;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = true;
			input.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::KEEP_CURRENT),
				static_cast<int>(EvaluateSceneBoundary(input).action));

			input.latestClassification =
				ActivePictureClassification::UNAVAILABLE;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::KEEP_CURRENT),
				static_cast<int>(EvaluateSceneBoundary(input).action));

			input.latestClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::PRESERVE_REFERENCE),
				static_cast<int>(EvaluateSceneBoundary(input).action));
		}

		TEST_METHOD(CurrentOverlayAtCutKeepsGeometryWithoutSnapshotExpiry)
		{
			SceneInput input;
			input.geometryAvailable = true;
			input.geometryIsCurrentGeneration = true;
			input.latestEvidenceIsCurrent = true;
			input.existingCropCanBeSnapshotted = true;
			input.currentOverlayEvidenceSupportsGeometry = true;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			input.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;

			const SceneDecision scene = EvaluateSceneBoundary(input);
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::KEEP_CURRENT),
				static_cast<int>(scene.action));
			Assert::IsTrue(scene.reason.find("overlay evidence") !=
				std::string::npos);

			// KEEP_CURRENT preserves the published geometry rather than creating a
			// two-second scene snapshot whose expiry can flash full raster.
			Input crop = TrustedScopeCrop();
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsProvisional = true;
			crop.verticalTranslationActive = true;
			crop.verticalTranslationPixels = 100;
			crop.verticalTranslationBase = crop.geometry;
			crop.verticalTranslationSourceGeneration = 7;
			const Decision presentation = Evaluate(crop);
			Assert::IsTrue(presentation.applyCrop);
			Assert::AreEqual(1610, presentation.sourceBounds.bottom -
				presentation.sourceBounds.top);
		}

		TEST_METHOD(VisiblePixelsAtCutOverrideSnapshotTimer)
		{
			SceneInput input;
			input.geometryAvailable = true;
			input.geometryIsCurrentGeneration = true;
			input.latestEvidenceIsCurrent = true;
			input.existingCropCanBeSnapshotted = true;
			input.frameLocalPresentationRetentionEvaluated = true;
			input.frameLocalPresentationRetentionSafe = false;
			input.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.latestClassification =
				ActivePictureClassification::PROVISIONAL;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::PRESERVE_REFERENCE),
				static_cast<int>(EvaluateSceneBoundary(input).action));
		}

		TEST_METHOD(SceneBoundaryPreservesCurrentLogicalReferenceButRejectsStaleState)
		{
			SceneInput input;
			input.geometryAvailable = true;
			input.geometryIsCurrentGeneration = true;
			input.latestEvidenceIsCurrent = true;
			input.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.latestClassification =
				ActivePictureClassification::FULL_RASTER_TRUSTED;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::PRESERVE_REFERENCE),
				static_cast<int>(EvaluateSceneBoundary(input).action));

			input.latestClassification =
				ActivePictureClassification::UNAVAILABLE;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::PRESERVE_REFERENCE),
				static_cast<int>(EvaluateSceneBoundary(input).action));

			input.latestClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.latestObservationSupportsCrop = true;
			input.geometryIsCurrentGeneration = false;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::WITHDRAW),
				static_cast<int>(EvaluateSceneBoundary(input).action));
		}

		TEST_METHOD(SceneHoldExpiryWithdrawsCropAndNlsTogether)
		{
			SceneHoldInput input;
			input.snapshotAvailable = true;
			input.nlsRequested = true;
			input.retainedMappingCompatible = true;
			input.snapshotSourceGeneration = 23;
			input.frameSourceGeneration = 23;
			input.currentTick = 1499;
			input.deadlineTick = 1500;

			SceneHoldDecision decision = EvaluateSceneHold(input);
			Assert::IsTrue(decision.cropActive);
			Assert::IsTrue(decision.nlsActive);

			input.currentTick = input.deadlineTick;
			decision = EvaluateSceneHold(input);
			Assert::IsFalse(decision.cropActive);
			Assert::IsFalse(decision.nlsActive);

			input.currentTick = 1499;
			input.retainedMappingCompatible = false;
			decision = EvaluateSceneHold(input);
			Assert::IsTrue(decision.cropActive);
			Assert::IsFalse(decision.nlsActive);
		}

		TEST_METHOD(SceneHoldWithoutNlsKeepsCropOnlyUntilDeadline)
		{
			SceneHoldInput input;
			input.snapshotAvailable = true;
			input.nlsRequested = false;
			input.retainedMappingCompatible = false;
			input.snapshotSourceGeneration = 31;
			input.frameSourceGeneration = 31;
			input.currentTick = 1999;
			input.deadlineTick = 2000;

			SceneHoldDecision decision = EvaluateSceneHold(input);
			Assert::IsTrue(decision.cropActive);
			Assert::IsFalse(decision.nlsActive);

			input.currentTick = input.deadlineTick;
			decision = EvaluateSceneHold(input);
			Assert::IsFalse(decision.cropActive);
			Assert::IsFalse(decision.nlsActive);
		}

		TEST_METHOD(DarkFadeHoldExpiresCropAndFinalNlsTogether)
		{
			SceneInput boundary;
			boundary.geometryAvailable = true;
			boundary.geometryIsCurrentGeneration = true;
			boundary.latestEvidenceIsCurrent = true;
			boundary.existingCropCanBeSnapshotted = true;
			boundary.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			boundary.latestClassification =
				ActivePictureClassification::UNAVAILABLE;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::HOLD_SNAPSHOT),
				static_cast<int>(EvaluateSceneBoundary(boundary).action));

			Input crop = TrustedScopeCrop();
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsUnavailable = true;
			crop.sceneVerificationHoldActive = true;
			Assert::IsTrue(Evaluate(crop).applyCrop);

			SceneHoldInput nlsOn;
			nlsOn.snapshotAvailable = true;
			nlsOn.nlsRequested = true;
			nlsOn.retainedMappingCompatible = true;
			nlsOn.snapshotSourceGeneration = 41;
			nlsOn.frameSourceGeneration = 41;
			nlsOn.currentTick = 2499;
			nlsOn.deadlineTick = 2500;
			Assert::IsTrue(EvaluateSceneHold(nlsOn).cropActive);
			Assert::IsTrue(EvaluateSceneHold(nlsOn).nlsActive);

			SceneHoldInput nlsOff = nlsOn;
			nlsOff.nlsRequested = false;
			nlsOff.retainedMappingCompatible = false;
			Assert::IsTrue(EvaluateSceneHold(nlsOff).cropActive);
			Assert::IsFalse(EvaluateSceneHold(nlsOff).nlsActive);

			crop.sceneVerificationHoldActive = false;
			AssertFullRaster(Evaluate(crop));
			nlsOn.currentTick = nlsOn.deadlineTick;
			Assert::IsFalse(EvaluateSceneHold(nlsOn).cropActive);
			Assert::IsFalse(EvaluateSceneHold(nlsOn).nlsActive);
		}

		TEST_METHOD(SceneSnapshotDeadlineDoesNotWithdrawPixelSafeLogicalScope)
		{
			SceneInput boundary;
			boundary.geometryAvailable = true;
			boundary.geometryIsCurrentGeneration = true;
			boundary.latestEvidenceIsCurrent = true;
			boundary.existingCropCanBeSnapshotted = true;
			boundary.geometryClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			boundary.latestClassification =
				ActivePictureClassification::UNAVAILABLE;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::HOLD_SNAPSHOT),
				static_cast<int>(EvaluateSceneBoundary(boundary).action));

			SceneHoldInput expired;
			expired.snapshotAvailable = true;
			expired.snapshotSourceGeneration = 7;
			expired.frameSourceGeneration = 7;
			expired.deadlineTick = 2000;
			expired.currentTick = 2000;
			Assert::IsFalse(EvaluateSceneHold(expired).cropActive);

			// Snapshot expiry is not geometry authority. With the same retained
			// logical scope and current pixel-safe evidence, presentation remains
			// scope without reacquiring the initial four-sample model.
			Input crop = TrustedScopeCrop();
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsUnavailable = true;
			crop.frameLocalPresentationRetentionEvaluated = true;
			crop.frameLocalPresentationRetentionSafe = true;
			crop.sceneVerificationHoldActive = false;
			const Decision retained = Evaluate(crop);
			Assert::IsTrue(retained.applyCrop);
			Assert::AreEqual(crop.geometry.top, retained.sourceBounds.top);
			Assert::AreEqual(crop.geometry.bottom, retained.sourceBounds.bottom);
		}

		TEST_METHOD(NearBlackStartupEpisodeStaysFullRasterUntilSceneBoundary)
		{
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.sourceGeneration = 19;
			input.sourceSequence = 100;

			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.started);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));

			// Sparse title strokes may push the sampled P90 above the threshold.
			// That single observation cannot re-arm crop acquisition.
			input.previous = decision.state;
			input.globalNearBlack = false;
			input.sourceSequence = 104;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));

			input.previous = decision.state;
			input.sceneBoundary = true;
			input.sourceSequence = 108;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.ended);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackRetainedCropCanOnlyMoveOnceToFullRaster)
		{
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.trustedCropAvailable = true;
			input.sourceGeneration = 23;
			input.sourceSequence = 200;

			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::RETAIN_CROP),
				static_cast<int>(decision.state.mode));

			input.previous = decision.state;
			input.boundedVisibleContentOutsideCrop = true;
			input.sourceSequence = 204;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.changedToFullRaster);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));

			input.previous = decision.state;
			input.boundedVisibleContentOutsideCrop = false;
			input.sourceSequence = 208;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackEpisodeSurvivesProfileGeometryWithdrawal)
		{
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.trustedCropAvailable = true;
			input.sourceGeneration = 29;
			input.sourceSequence = 300;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::RETAIN_CROP),
				static_cast<int>(decision.state.mode));

			// Profile publication may withdraw profile-dependent geometry without
			// replacing the decoded source generation. The title episode remains
			// active and makes the only safe monotonic transition.
			input.previous = decision.state;
			input.measurementCurrent = false;
			input.trustedCropAvailable = false;
			input.sourceSequence = 301;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.changedToFullRaster);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackFullRasterReleasesOnlyExactEntryCropAfterSafeDwell)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.trustedCropAvailable = true;
			input.trustedCrop = scope;
			input.presentationEpoch = 41;
			input.sourceGeneration = 31;
			input.sourceSequence = 1000;
			input.framesPerSecond = 23.976;

			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::RETAIN_CROP),
				static_cast<int>(decision.state.mode));

			input.previous = decision.state;
			input.boundedVisibleContentOutsideCrop = true;
			input.sourceSequence = 1001;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.changedToFullRaster);
			for (uint64_t sequence = 1002; sequence <= 1008; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::IsFalse(decision.state.confirmedNonNearBlackContent);
			}

			input.previous = decision.state;
			input.globalNearBlack = false;
			input.sourceSequence = 1009;
			input.retentionEvaluated = true;
			input.retentionSafe = false;
			input.retentionBounds = scope;
			input.retentionSourceGeneration = input.sourceGeneration;
			input.retentionSourceSequence = input.sourceSequence;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u,
				decision.state.outwardConfirmationSamples);

			input.boundedVisibleContentOutsideCrop = false;
			input.currentObservationAvailable = true;
			input.currentObservation = scope;
			input.retentionEvaluated = true;
			input.retentionSafe = true;
			input.retentionBounds = scope;
			input.retentionSourceGeneration = input.sourceGeneration;
			input.knownTrustedGeometryReacquired = true;
			input.reacquiredTrustedGeometry = scope;
			input.reacquiredTrustedClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.reacquiredSourceGeneration = input.sourceGeneration;
			input.reacquiredSourceSequence = 1010;
			input.reacquiredPresentationEpoch = input.presentationEpoch;
			input.reacquisitionIsCurrentAssociation = true;

			for (uint64_t sequence = 1010; sequence <= 1016; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				input.reacquiredSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				if (sequence < 1016)
				{
					Assert::IsFalse(decision.releasedToTrustedCrop);
					Assert::AreEqual(static_cast<int>(
						NearBlackPresentationMode::FULL_RASTER),
						static_cast<int>(decision.state.mode));
				}
			}

			Assert::IsTrue(decision.releasedToTrustedCrop);
			Assert::IsTrue(decision.ended);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));

			Input crop = TrustedScopeCrop();
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsProvisional = true;
			crop.frameLocalPresentationRetentionEvaluated = true;
			crop.frameLocalPresentationRetentionSafe = true;
			const Decision restored = Evaluate(crop);
			Assert::IsTrue(restored.applyCrop);
			Assert::AreEqual(scope.top, restored.sourceBounds.top);
			Assert::AreEqual(scope.bottom, restored.sourceBounds.bottom);
		}

		TEST_METHOD(NearBlackSustainedOutwardContentRecoversAfterContainedSafeDwell)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.trustedCropAvailable = true;
			input.trustedCrop = scope;
			input.presentationEpoch = 45;
			input.sourceGeneration = 39;
			input.sourceSequence = 2100;
			input.framesPerSecond = 23.976;
			auto decision = EvaluateNearBlackPresentationEpisode(input);

			input.globalNearBlack = false;
			input.boundedVisibleContentOutsideCrop = true;
			input.retentionEvaluated = true;
			input.retentionSafe = false;
			input.retentionBounds = scope;
			input.retentionSourceGeneration = input.sourceGeneration;
			for (uint64_t sequence = 2101; sequence <= 2113; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
			}
			Assert::IsTrue(decision.state.confirmedNonNearBlackContent);

			input.boundedVisibleContentOutsideCrop = false;
			input.currentObservationAvailable = true;
			input.currentObservation = scope;
			input.currentObservation.top += 4;
			input.retentionEvaluated = true;
			input.retentionSafe = true;
			input.retentionBounds = scope;
			input.knownTrustedGeometryReacquired = true;
			input.reacquiredTrustedGeometry = scope;
			input.reacquiredTrustedClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.reacquiredSourceGeneration = input.sourceGeneration;
			input.reacquiredPresentationEpoch = input.presentationEpoch;
			input.reacquisitionIsCurrentAssociation = true;
			for (uint64_t sequence = 2114; sequence <= 2120; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				input.reacquiredSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::AreEqual(sequence == 2120, decision.releasedToTrustedCrop);
			}
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackOutwardConfirmationRequiresExactCurrentEntryRetention)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.trustedCropAvailable = true;
			input.trustedCrop = scope;
			input.presentationEpoch = 46;
			input.sourceGeneration = 40;
			input.sourceSequence = 2200;
			input.framesPerSecond = 23.976;
			auto decision = EvaluateNearBlackPresentationEpisode(input);

			input.globalNearBlack = false;
			input.boundedVisibleContentOutsideCrop = true;
			input.retentionEvaluated = true;
			input.retentionBounds = scope;
			input.retentionBounds.top += 2;
			input.retentionSourceGeneration = input.sourceGeneration;
			for (uint64_t sequence = 2201; sequence <= 2214; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
			}
			Assert::IsFalse(decision.state.confirmedNonNearBlackContent);
			Assert::AreEqual(0u, decision.state.outwardConfirmationSamples);

			input.retentionBounds = scope;
			input.previous = decision.state;
			input.sourceSequence = 2215;
			input.retentionSourceSequence = 2214;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.outwardConfirmationSamples);

			input.previous = decision.state;
			input.sourceSequence = 2216;
			input.retentionSourceSequence = 2216;
			input.retentionSourceGeneration = input.sourceGeneration + 1;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.outwardConfirmationSamples);
		}

		TEST_METHOD(CaptionBackedLogicalAcquisitionRequiresFreshProofDuringEpisode)
		{
			for (const auto mode : { NearBlackPresentationMode::RETAIN_CROP,
				NearBlackPresentationMode::FULL_RASTER })
			{
				Assert::IsFalse(NearBlackBlocksCaptionBackedLogicalAcquisition(
                    true, mode, true),
                    L"Current strict native-bar and caption proof permits logical acquisition under darkness.");
                Assert::IsTrue(NearBlackBlocksCaptionBackedLogicalAcquisition(
                    true, mode, false),
                    L"Darkness still blocks logical crop when current strict proof is missing.");
				Assert::IsTrue(NearBlackBlocksCaptionBackedLogicalAcquisition(
					false, mode, false),
					L"An active episode needs current strict caption/bar proof.");
				Assert::IsFalse(NearBlackBlocksCaptionBackedLogicalAcquisition(
					false, mode, true),
					L"Current strict proof may acquire logical geometry during an active episode.");
			}

			Assert::IsFalse(NearBlackBlocksCaptionBackedLogicalAcquisition(
                true, NearBlackPresentationMode::INACTIVE, true));
			Assert::IsFalse(NearBlackBlocksCaptionBackedLogicalAcquisition(
				false, NearBlackPresentationMode::INACTIVE, false),
				L"Ordinary non-episode acquisition remains unchanged.");
			Assert::IsFalse(NearBlackBlocksCaptionBackedLogicalAcquisition(
				false, NearBlackPresentationMode::INACTIVE, true));
		}
		TEST_METHOD(CaptionBootstrapScanUsesBoundedProofBurstAndColdRateLimit)
		{
			CaptionBootstrapScanInput input;
			input.sourceGeneration = 5;
			input.analysisDomainKey = 8;
			input.acceptedSequence = 100;
			input.bridgeFrames = 12;
			input.coldProbeIntervalFrames = 24;

			Assert::AreEqual(int(CaptionBootstrapScanAction::COLD_PROBE),
				int(SelectCaptionBootstrapScan(input)),
				L"A new no-presentation source starts with one standalone probe.");

			input.attemptGeneration = input.sourceGeneration;
			input.attemptDomainKey = input.analysisDomainKey;
			input.lastStandaloneSequence = 100;
			input.acceptedSequence = 123;
			Assert::AreEqual(int(CaptionBootstrapScanAction::NONE),
				int(SelectCaptionBootstrapScan(input)),
				L"Cold retries remain rate-limited before one second.");
			input.acceptedSequence = 124;
			Assert::AreEqual(int(CaptionBootstrapScanAction::COLD_PROBE),
				int(SelectCaptionBootstrapScan(input)),
				L"An expired proof may be retried at the cold-probe cadence.");

			// Production records the sequence of every standalone probe.
			input.lastStandaloneSequence = 124;
			input.firstProofSequence = 124;
			Assert::AreEqual(int(CaptionBootstrapScanAction::BRIDGE),
				int(SelectCaptionBootstrapScan(input)));
			input.acceptedSequence = 125;
			Assert::AreEqual(int(CaptionBootstrapScanAction::BRIDGE),
				int(SelectCaptionBootstrapScan(input)),
				L"A successful cold proof immediately opens the adjacent confirmation burst.");
			input.acceptedSequence = 135; // Exactly 12 accepted sequences including 124.
			Assert::AreEqual(int(CaptionBootstrapScanAction::BRIDGE),
				int(SelectCaptionBootstrapScan(input)));
			// Production also advances this marker on each bridge scan.
			input.lastStandaloneSequence = 135;
			input.acceptedSequence = 136;
			Assert::AreEqual(int(CaptionBootstrapScanAction::NONE),
				int(SelectCaptionBootstrapScan(input)),
				L"The contiguous bridge ends at its exclusive 12-frame bound.");
			input.acceptedSequence = 158;
			Assert::AreEqual(int(CaptionBootstrapScanAction::NONE),
				int(SelectCaptionBootstrapScan(input)),
				L"Expiry does not turn the burst into a per-frame retry before the cold cadence.");
			input.acceptedSequence = 159;
			Assert::AreEqual(int(CaptionBootstrapScanAction::COLD_PROBE),
				int(SelectCaptionBootstrapScan(input)));

			input.acceptedSequence = 130;
			input.hasEstablishedGeometry = true;
			input.establishedMatchesProof = true;
			// Production records the sequence of every standalone probe.
			input.lastStandaloneSequence = 124;
			input.firstProofSequence = 124;
			Assert::AreEqual(int(CaptionBootstrapScanAction::BRIDGE),
				int(SelectCaptionBootstrapScan(input)),
				L"An established presentation continues the bridge only for identical geometry.");
			input.establishedMatchesProof = false;
			Assert::AreEqual(int(CaptionBootstrapScanAction::NONE),
				int(SelectCaptionBootstrapScan(input)),
				L"Established geometry mismatch terminates the proof burst.");

			input.hasEstablishedGeometry = false;
			input.establishedMatchesProof = false;
			input.sourceGeneration = 6;
			input.acceptedSequence = 131;
			Assert::AreEqual(int(CaptionBootstrapScanAction::COLD_PROBE),
				int(SelectCaptionBootstrapScan(input)),
				L"A new source generation can start a fresh cold probe immediately.");
			input.sourceGeneration = 5;
			input.analysisDomainKey = 9;
			Assert::AreEqual(int(CaptionBootstrapScanAction::COLD_PROBE),
				int(SelectCaptionBootstrapScan(input)),
				L"A changed analysis domain cannot reuse the old proof or rate limit.");

			input.sourceGeneration = 0;
			Assert::AreEqual(int(CaptionBootstrapScanAction::NONE),
				int(SelectCaptionBootstrapScan(input)));
			input.sourceGeneration = 5;
			input.analysisDomainKey = 0;
			Assert::AreEqual(int(CaptionBootstrapScanAction::NONE),
				int(SelectCaptionBootstrapScan(input)));
			input.analysisDomainKey = 8;
			input.acceptedSequence = 0;
			Assert::AreEqual(int(CaptionBootstrapScanAction::NONE),
				int(SelectCaptionBootstrapScan(input)));
		}
		TEST_METHOD(NearBlackEpisodeSuppressesPublishedAndHeldBarGeometryMutation)
		{
			Assert::IsTrue(ShouldSuppressNearBlackBarGeometryMutation(true, true,
				ActivePictureClassification::BAR_CROP_TRUSTED));
			// The following non-publish stable frame has the same typed decision.
			Assert::IsTrue(ShouldSuppressNearBlackBarGeometryMutation(true, true,
				ActivePictureClassification::BAR_CROP_TRUSTED));
			Assert::IsFalse(ShouldSuppressNearBlackBarGeometryMutation(true, true,
				ActivePictureClassification::FULL_RASTER_TRUSTED));
			Assert::IsFalse(ShouldSuppressNearBlackBarGeometryMutation(false, true,
				ActivePictureClassification::BAR_CROP_TRUSTED));
		}

		TEST_METHOD(NearBlackStartupBootstrapOnlyReopensNormalAcquisition)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.presentationEpoch = 47;
			input.sourceGeneration = 41;
			input.sourceSequence = 1;
			input.framesPerSecond = 23.976;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));
			Assert::IsFalse(decision.state.entryTrustedCropAvailable);

			input.globalNearBlack = false;
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = scope;
			input.nativeBootstrapRetentionEvaluated = true;
			input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = input.sourceGeneration;
			input.nativeBootstrapPresentationEpoch = input.presentationEpoch;
			for (uint64_t sequence = 2; sequence <= 11; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.nativeBootstrapSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				if (sequence < 11)
				{
					Assert::IsFalse(decision.bootstrapReleased);
					Assert::AreEqual(static_cast<int>(
						NearBlackPresentationMode::FULL_RASTER),
						static_cast<int>(decision.state.mode));
				}
			}
			Assert::IsTrue(decision.bootstrapReleased);
			Assert::IsTrue(decision.resetTransitionEvidence);
			Assert::IsFalse(decision.releasedToTrustedCrop);
			Assert::IsTrue(decision.ended);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackSimultaneousOutwardEntryRecoversLoggedAlienCrop)
		{
			for (bool injectMovement : {false,true})
			{
			auto input = ReaffirmedRetainedScope(true);
			if (injectMovement)
			{
				MovingPictureTransitionState moving;
				for (unsigned index=0;index<10;++index)
				{
					const uint64_t seq=1952+index;
					const auto observation=MovingRecoveryObservation(input.trustedCrop,1,seq,index,input.framesPerSecond);
					moving=ObserveMovingPictureTransition(moving,{1,seq,seq,seq*417083,11,10,17},observation);
				}
				Assert::IsTrue(moving.active);
				auto dark=MovingRecoveryObservation(input.trustedCrop,1,1962,10,input.framesPerSecond);
				dark.retention.globalNearBlack=true;
				moving=ObserveMovingPictureTransition(moving,{1,1962,1962,1962*417083,11,10,17},dark);
				Assert::IsFalse(moving.active,L"Near-black episode must replace movement without retaining its quiet dwell.");
				Assert::IsFalse(input.fullRasterAuthorityAvailable,L"Temporary full presentation must not fabricate full-raster authority.");
			}
			Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER), int(input.previous.mode));
			// The paused Alien observation is ambiguous inside the known 2.40 crop.
			// It cannot acquire a crop; current excluded-band safety may revalidate one.
			input.currentObservation = { 0, 416, 2668, 1880, 3840, 2160,
				2668.0 / 1464.0, ActivePictureBounds::BarAxes::NONE };
			input.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
			for (uint64_t seq = 1963; seq <= 1969; ++seq)
			{
				input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = seq;
				const auto decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::AreEqual(seq == 1969, decision.releasedToTrustedCrop);
				Assert::IsFalse(decision.bootstrapReleased);
				if (seq < 1969)
					Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER), int(decision.state.mode));
				input.previous = decision.state;
			}
			}
		}

		TEST_METHOD(NearBlackSimultaneousOutwardEntryRecoveryKeepsEvidenceVetoes)
		{
			for (int failure = 0; failure < 17; ++failure)
			{
				auto input = ReaffirmedRetainedScope(true);
				input.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
				for (uint64_t seq = 1963; seq <= 1982; ++seq)
				{
					input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = seq;
					auto invalid = input;
					switch (failure)
					{
					case 0: invalid.measurementCurrent = false; break;
					case 1: invalid.retentionEvaluated = false; break;
					case 2: invalid.retentionSafe = invalid.retentionExcludedBandsPixelSafe = false; break;
					case 3: invalid.retentionSourceSequence--; break;
					case 4: invalid.retentionSourceGeneration++; break;
					case 5: invalid.retentionBounds.top += 2; break;
					case 6: invalid.currentObservationAvailable = false; break;
					case 7: invalid.currentObservation.bottom += 20; break;
					case 8: invalid.knownTrustedGeometryReacquired = false; break;
					case 9: invalid.reacquisitionIsCurrentAssociation = false; break;
					case 10: invalid.reacquiredSourceSequence = 1961; break;
					case 11: invalid.reacquiredSourceGeneration++; break;
					case 12: invalid.reacquiredPresentationEpoch++; break;
					case 13: invalid.reacquiredTrustedGeometry.top += 20; break;
					case 14: invalid.boundedVisibleContentOutsideCrop = true; break;
					case 15: invalid.globalNearBlack = true; break;
					case 16: invalid.cadenceRepeat = true; break;
					}
					const auto decision = EvaluateNearBlackPresentationEpisode(invalid);
					Assert::IsFalse(decision.releasedToTrustedCrop);
					Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER), int(decision.state.mode));
					Assert::AreEqual(0u, decision.state.revalidationSamples,
                        (L"invalid evidence variant " + std::to_wstring(failure)).c_str());
                    input.previous = decision.state;
				}
			}
		}

		TEST_METHOD(NearBlackSimultaneousOutwardEntryDoesNotCarryRecoveryAcrossContext)
		{
			for (int boundary = 0; boundary < 4; ++boundary)
			{
				auto input = ReaffirmedRetainedScope(true);
				input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = 1963;
				if (boundary == 0) input.sceneBoundary = true;
				if (boundary == 1) input.sourceGeneration++;
				if (boundary == 2) input.presentationEpoch++;
				if (boundary == 3) input.fullRasterAuthorityAvailable = true;
				const auto decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::IsFalse(decision.releasedToTrustedCrop);
				Assert::IsFalse(decision.state.entryTrustedCropAvailable);
				Assert::AreEqual(0u, decision.state.revalidationSamples);
			}
		}

		TEST_METHOD(NearBlackNativeBootstrapIsNotDelayedByPartialEntryRecovery)
		{
			auto input = ReaffirmedRetainedScope(true);
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = input.trustedCrop;
			input.nativeBootstrapRetentionEvaluated = input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = input.sourceGeneration;
			input.nativeBootstrapPresentationEpoch = input.presentationEpoch;
			for (uint64_t seq = 1963; seq <= 1972; ++seq)
			{
				input.sourceSequence = input.retentionSourceSequence =
					input.reacquiredSourceSequence = input.nativeBootstrapSourceSequence = seq;
				// The old-crop association is intermittent, but native acquisition
				// has ten uninterrupted independent safe measurements.
				input.reacquisitionIsCurrentAssociation = seq % 2 == 0;
				const auto decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::IsFalse(decision.releasedToTrustedCrop);
				Assert::AreEqual(seq == 1972, decision.bootstrapReleased);
				input.previous = decision.state;
			}
		}

		TEST_METHOD(NearBlackOutwardOverlayCanBootstrapBackToSafeNativeCrop)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.trustedCropAvailable = true;
			input.trustedCrop = scope;
			input.boundedVisibleContentOutsideCrop = true;
			input.presentationEpoch = 52;
			input.sourceGeneration = 51;
			input.sourceSequence = 1;
			input.framesPerSecond = 23.976;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));
			Assert::IsTrue(decision.state.entryTrustedCropAvailable);
			Assert::IsTrue(decision.state.startedAtFullRaster);

			input.globalNearBlack = false;
			input.boundedVisibleContentOutsideCrop = false;
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = scope;
			input.nativeBootstrapRetentionEvaluated = true;
			input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = input.sourceGeneration;
			input.nativeBootstrapPresentationEpoch = input.presentationEpoch;
			for (uint64_t sequence = 2; sequence <= 11; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.nativeBootstrapSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
			}

			Assert::AreEqual(10u, decision.bootstrapSamplesRequired);
			Assert::IsTrue(decision.bootstrapReleased);
			Assert::IsTrue(decision.resetTransitionEvidence);
			Assert::IsFalse(decision.releasedToTrustedCrop);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackStartupBootstrapAcceptsExactPausedFrameAfterTimedDwell)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 42;
			input.previous.presentationEpoch = 48;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = scope;
			input.nativeBootstrapRetentionEvaluated = true;
			input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = 42;
			input.nativeBootstrapSourceSequence = 5000;
			input.nativeBootstrapPresentationEpoch = 48;
			input.presentationEpoch = 48;
			input.sourceGeneration = 42;
			input.sourceSequence = 5000;
			input.currentTick = 1000;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u, decision.state.bootstrapSamples);

			input.previous = decision.state;
			input.cadenceRepeat = true;
			for (uint64_t tick = 1050; tick <= 1450; tick += 50)
			{
				input.currentTick = tick;
				decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::IsFalse(decision.bootstrapReleased);
				Assert::AreEqual(1u, decision.state.bootstrapSamples);
				input.previous = decision.state;
			}
			input.currentTick = 1499;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsFalse(decision.bootstrapReleased);
			Assert::AreEqual(1u, decision.state.bootstrapSamples);

			input.previous = decision.state;
			input.currentTick = 1500;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.bootstrapReleased);
			Assert::IsTrue(decision.resetTransitionEvidence);
			Assert::IsFalse(decision.releasedToTrustedCrop);
			Assert::IsTrue(decision.ended);
		}

		TEST_METHOD(NearBlackStartupBootstrapRestartsOnCurrentProfileEpoch)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 42;
			input.previous.presentationEpoch = 1;
			input.previous.fullRasterStartedSourceSequence = 1;
			input.previous.bootstrapCandidateAvailable = true;
			input.previous.bootstrapCandidate = scope;
			input.previous.bootstrapLastSourceSequence = 42;
			input.previous.bootstrapSamples = 10;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = scope;
			input.nativeBootstrapRetentionEvaluated = true;
			input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = 42;
			input.nativeBootstrapSourceSequence = 256;
			input.nativeBootstrapPresentationEpoch = 3;
			input.presentationEpoch = 3;
			input.sourceGeneration = 42;
			input.sourceSequence = 256;
			input.framesPerSecond = 23.976;

			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.revalidationChanged);
			Assert::AreEqual(3ull, decision.state.presentationEpoch);
			Assert::AreEqual(1u, decision.state.bootstrapSamples);
			Assert::AreEqual(256ull,
				decision.state.fullRasterStartedSourceSequence);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));

			for (uint64_t sequence = 257; sequence <= 265; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.nativeBootstrapSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
			}
			Assert::IsTrue(decision.bootstrapReleased);
			Assert::IsTrue(decision.resetTransitionEvidence);
			Assert::IsTrue(decision.ended);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackProfileEpochRestartIsObservableWithoutCropEvidence)
		{
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 42;
			input.previous.presentationEpoch = 1;
			input.previous.fullRasterStartedSourceSequence = 1;
			input.presentationEpoch = 3;
			input.sourceGeneration = 42;
			input.sourceSequence = 256;
			input.framesPerSecond = 23.976;

			const auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.revalidationChanged);
			Assert::IsFalse(decision.bootstrapReleased);
			Assert::IsFalse(decision.state.bootstrapCandidateAvailable);
			Assert::AreEqual(3ull, decision.state.presentationEpoch);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(NearBlackPausedBootstrapOverlayResetsTimedDwell)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 42;
			input.previous.presentationEpoch = 48;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = scope;
			input.nativeBootstrapRetentionEvaluated = true;
			input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = 42;
			input.nativeBootstrapSourceSequence = 5100;
			input.nativeBootstrapPresentationEpoch = 48;
			input.presentationEpoch = 48;
			input.sourceGeneration = 42;
			input.sourceSequence = 5100;
			input.currentTick = 2000;
			auto decision = EvaluateNearBlackPresentationEpisode(input);

			input.previous = decision.state;
			input.cadenceRepeat = true;
			input.currentTick = 2499;
			input.nativeBootstrapContractAvailable = false;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.bootstrapSamples);
			Assert::AreEqual(0ull,
				decision.state.bootstrapCandidateStartedTick);

			input.previous = decision.state;
			input.nativeBootstrapContractAvailable = true;
			input.currentTick = 2500;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsFalse(decision.bootstrapReleased);
			Assert::AreEqual(0u, decision.state.bootstrapSamples);

			input.previous = decision.state;
			input.cadenceRepeat = false;
			input.currentTick = 2501;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u, decision.state.bootstrapSamples);
		}

		TEST_METHOD(NearBlackPausedBootstrapRejectsRepeatOnlyAndStalledEvidence)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 42;
			input.previous.presentationEpoch = 48;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = scope;
			input.nativeBootstrapRetentionEvaluated = true;
			input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = 42;
			input.nativeBootstrapSourceSequence = 5200;
			input.nativeBootstrapPresentationEpoch = 48;
			input.presentationEpoch = 48;
			input.sourceGeneration = 42;
			input.sourceSequence = 5200;
			input.currentTick = 3000;
			input.cadenceRepeat = true;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.bootstrapSamples);
			Assert::IsFalse(decision.state.bootstrapCandidateAvailable);

			input.previous = decision.state;
			input.cadenceRepeat = false;
			input.currentTick = 3100;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u, decision.state.bootstrapSamples);

			input.previous = decision.state;
			input.cadenceRepeat = true;
			input.currentTick = 3601;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsFalse(decision.bootstrapReleased);
			Assert::AreEqual(0u, decision.state.bootstrapSamples);
			Assert::IsFalse(decision.state.bootstrapCandidateAvailable);
		}

		TEST_METHOD(NearBlackStartupBootstrapResetsOnMismatchAndNearBlack)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 43;
			input.previous.presentationEpoch = 49;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.nativeBootstrapContractAvailable = true;
			input.nativeBootstrapContract = scope;
			input.nativeBootstrapRetentionEvaluated = true;
			input.nativeBootstrapRetentionSafe = true;
			input.nativeBootstrapSourceGeneration = 43;
			input.nativeBootstrapPresentationEpoch = 49;
			input.presentationEpoch = 49;
			input.sourceGeneration = 43;
			input.sourceSequence = 6000;
			input.nativeBootstrapSourceSequence = 6000;
			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u, decision.state.bootstrapSamples);

			input.previous = decision.state;
			input.sourceSequence = 6001;
			input.nativeBootstrapSourceSequence = 6001;
			input.nativeBootstrapContract.top += 2;
			input.nativeBootstrapContract.bottom -= 2;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u, decision.state.bootstrapSamples);

			input.previous = decision.state;
			input.sourceSequence = 6002;
			input.nativeBootstrapSourceSequence = 6002;
			input.cadenceRepeat = true;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.bootstrapSamples);

			input.previous = decision.state;
			input.sourceSequence = 6003;
			input.nativeBootstrapSourceSequence = 6003;
			input.cadenceRepeat = false;
			input.globalNearBlack = true;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.bootstrapSamples);
			Assert::IsFalse(decision.bootstrapReleased);
		}

		TEST_METHOD(NearBlackCropRevalidationResetsAcrossSubtitleGap)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = true;
			input.trustedCropAvailable = true;
			input.trustedCrop = scope;
			input.presentationEpoch = 43;
			input.sourceGeneration = 37;
			input.retentionSourceGeneration = input.sourceGeneration;
			input.sourceSequence = 2000;
			input.framesPerSecond = 23.976;
			auto decision = EvaluateNearBlackPresentationEpisode(input);

			input.previous = decision.state;
			input.globalNearBlack = false;
			input.boundedVisibleContentOutsideCrop = true;
			input.sourceSequence = 2001;
			decision = EvaluateNearBlackPresentationEpisode(input);

			input.boundedVisibleContentOutsideCrop = false;
			input.currentObservationAvailable = true;
			input.currentObservation = scope;
			input.retentionEvaluated = true;
			input.retentionSafe = true;
			input.retentionBounds = scope;
			input.knownTrustedGeometryReacquired = true;
			input.reacquiredTrustedGeometry = scope;
			input.reacquiredTrustedClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.reacquiredSourceGeneration = input.sourceGeneration;
			input.reacquiredSourceSequence = 2002;
			input.reacquiredPresentationEpoch = input.presentationEpoch;
			input.reacquisitionIsCurrentAssociation = true;

			for (uint64_t sequence = 2002; sequence <= 2004; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
			}
			Assert::AreEqual(3u, decision.state.revalidationSamples);

			input.previous = decision.state;
			input.sourceSequence = 2005;
			input.retentionSourceSequence = 2005;
			input.boundedVisibleContentOutsideCrop = true;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.revalidationSamples);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::FULL_RASTER),
				static_cast<int>(decision.state.mode));

			input.boundedVisibleContentOutsideCrop = false;
			for (uint64_t sequence = 2006; sequence <= 2010; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::IsFalse(decision.releasedToTrustedCrop);
			}
		}

		TEST_METHOD(NearBlackEpisodeRetiresObsoleteNativeEntryAfterCurrentOutwardProof)
		{
			const ActivePictureBounds oldCrop = {
				0, 276, 3840, 1884, 3840, 2160, 2.3881,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds newCrop = {
				0, 68, 3840, 2092, 3840, 2160, 1.8972,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 47;
			input.previous.presentationEpoch = 53;
			input.previous.entryTrustedCropAvailable = true;
			input.previous.entryTrustedCrop = oldCrop;
			input.previous.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
			input.previous.fullRasterStartedSourceSequence = 1300;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = false;
			input.trustedCropAvailable = true;
			input.trustedCrop = oldCrop;
			input.trustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
			input.currentObservationAvailable = true;
			input.currentObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.currentObservation = newCrop;
			input.currentObservationOrigin = ActivePictureAuthorityOrigin::NATIVE;
			input.currentNativeObservationAvailable = true;
			input.currentNativeObservation = newCrop;
			input.boundedVisibleContentOutsideCrop = true;
			input.retentionEvaluated = true;
			input.retentionSafe = false;
			input.retentionBounds = oldCrop;
			input.retentionSourceGeneration = 47;
			input.retentionSourceSequence = 1339;
			input.presentationEpoch = 53;
			input.sourceGeneration = 47;
			input.sourceSequence = 1339;
			input.currentNativeOutwardProof.base = oldCrop;
			input.currentNativeOutwardProof.candidate = newCrop;
			input.currentNativeOutwardProof.referenceCandidate = newCrop;
			input.currentNativeOutwardProof.proofRoute = OutwardPictureProofRoute::BROAD_OCCUPANCY;
			input.currentNativeOutwardProof.confirmations =
				OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED;
			input.currentNativeOutwardProof.sourceGeneration = 47;
			input.currentNativeOutwardProof.firstPictureSourceSequence = 1337;
			input.currentNativeOutwardProof.lastObservedSourceSequence = 1339;
			input.currentNativeOutwardProofPresentationEpoch = 53;

			Assert::IsTrue(QualifiesObsoleteNearBlackEntryNativeOutwardPictureSample(input));
			auto first = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsFalse(first.outwardEntryReleased);
			Assert::AreEqual(1u, first.state.nativeOutwardReleaseSamples);
			auto next = input;
			next.previous = first.state;
			next.sourceSequence = 1340;
			next.retentionSourceSequence = 1340;
			next.currentNativeOutwardProof.lastObservedSourceSequence = 1340;
			auto second = EvaluateNearBlackPresentationEpisode(next);
			Assert::IsFalse(second.outwardEntryReleased);
			Assert::AreEqual(2u, second.state.nativeOutwardReleaseSamples);
			next.previous = second.state;
			next.sourceSequence = 1341;
			next.retentionSourceSequence = 1341;
			next.currentNativeOutwardProof.lastObservedSourceSequence = 1341;
			const auto decision = EvaluateNearBlackPresentationEpisode(next);
			Assert::IsTrue(decision.ended);
			Assert::IsTrue(decision.outwardEntryReleased);
			Assert::IsTrue(decision.bootstrapReleased);
			Assert::IsTrue(decision.resetTrustedGeometry);
			Assert::IsFalse(decision.releasedToTrustedCrop);
			Assert::AreEqual(static_cast<int>(NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));

            // A last-submitted Scope can restore FULL_RASTER to RETAIN_CROP.
            // Both owners must release after the same independent native proof.
            for (auto mode : {NearBlackPresentationMode::RETAIN_CROP,NearBlackPresentationMode::FULL_RASTER}) {
                auto retained=input;retained.previous.mode=mode;
                retained.previouslyAdmittedCropAvailable=true;
                retained.previouslyAdmittedCrop=oldCrop;
                retained.previouslyAdmittedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;
                retained.previouslyAdmittedCropSourceGeneration=input.sourceGeneration;
                retained.previouslyAdmittedCropPresentationEpoch=input.presentationEpoch;
                NearBlackPresentationEpisodeDecision result;
                for(uint64_t seq=1339;seq<=1341;++seq) {
                    retained.sourceSequence=retained.retentionSourceSequence=seq;
                    retained.currentNativeOutwardProof.lastObservedSourceSequence=seq;
                    result=EvaluateNearBlackPresentationEpisode(retained);
                    retained.previous=result.state;
                }
                Assert::IsTrue(result.outwardEntryReleased);
                Assert::IsTrue(result.resetTrustedGeometry);
            }

			auto latchedAfterProofBegan = input;
			latchedAfterProofBegan.previous.fullRasterStartedSourceSequence = 1338;
			Assert::IsTrue(QualifiesObsoleteNearBlackEntryNativeOutwardPictureSample(latchedAfterProofBegan));

			auto dark = input;
			dark.globalNearBlack = true;
			auto darkDecision = EvaluateNearBlackPresentationEpisode(dark);
			Assert::AreEqual(0u, darkDecision.state.nativeOutwardReleaseSamples);
			dark.previous = darkDecision.state;
			dark.sourceSequence = 1340;
			dark.retentionSourceSequence = 1340;
			dark.currentNativeOutwardProof.lastObservedSourceSequence = 1340;
			darkDecision = EvaluateNearBlackPresentationEpisode(dark);
			Assert::AreEqual(0u, darkDecision.state.nativeOutwardReleaseSamples);
			dark.globalNearBlack = false;
			dark.previous = darkDecision.state;
			dark.sourceSequence = 1341;
			dark.retentionSourceSequence = 1341;
			dark.currentNativeOutwardProof.lastObservedSourceSequence = 1341;
			const auto firstBright = EvaluateNearBlackPresentationEpisode(dark);
			Assert::IsFalse(firstBright.outwardEntryReleased);
			Assert::AreEqual(1u, firstBright.state.nativeOutwardReleaseSamples);
			dark.previous = firstBright.state;
			dark.sourceSequence = 1342;
			dark.retentionSourceSequence = 1342;
			dark.currentNativeOutwardProof.lastObservedSourceSequence = 1342;
			const auto secondBright = EvaluateNearBlackPresentationEpisode(dark);
			Assert::IsFalse(secondBright.outwardEntryReleased);
			dark.previous = secondBright.state;
			dark.sourceSequence = 1343;
			dark.retentionSourceSequence = 1343;
			dark.currentNativeOutwardProof.lastObservedSourceSequence = 1343;
			Assert::IsTrue(EvaluateNearBlackPresentationEpisode(dark).outwardEntryReleased);

			auto weak = input;
			weak.currentNativeOutwardProof.confirmations = 1;
			auto weakDecision = EvaluateNearBlackPresentationEpisode(weak);
			for (uint64_t sequence = 1340; sequence <= 1341; ++sequence)
			{
				weak.previous = weakDecision.state;
				weak.sourceSequence = sequence;
				weak.retentionSourceSequence = sequence;
				weak.currentNativeOutwardProof.lastObservedSourceSequence = sequence;
				weak.currentNativeOutwardProof.confirmations = 2;
				weakDecision = EvaluateNearBlackPresentationEpisode(weak);
			}
			Assert::IsFalse(weakDecision.outwardEntryReleased);
			Assert::AreEqual(3u, weakDecision.state.nativeOutwardReleaseSamples);

			// A 24 Hz source presented at 60 Hz may repeat each certified frame.
			auto cadence = input;
			auto cadenceDecision = EvaluateNearBlackPresentationEpisode(cadence);
			cadence.previous = cadenceDecision.state;
			cadence.cadenceRepeat = true;
			cadenceDecision = EvaluateNearBlackPresentationEpisode(cadence);
			Assert::AreEqual(1u, cadenceDecision.state.nativeOutwardReleaseSamples);
			cadence.previous = cadenceDecision.state;
			cadenceDecision = EvaluateNearBlackPresentationEpisode(cadence);
			Assert::AreEqual(1u, cadenceDecision.state.nativeOutwardReleaseSamples);
			cadence.cadenceRepeat = false;
			cadence.previous = cadenceDecision.state;
			cadence.sourceSequence = 1340;
			cadence.retentionSourceSequence = 1340;
			cadence.currentNativeOutwardProof.lastObservedSourceSequence = 1340;
			cadenceDecision = EvaluateNearBlackPresentationEpisode(cadence);
			Assert::AreEqual(2u, cadenceDecision.state.nativeOutwardReleaseSamples);
			cadence.previous = cadenceDecision.state;
			cadence.cadenceRepeat = true;
			cadenceDecision = EvaluateNearBlackPresentationEpisode(cadence);
			Assert::AreEqual(2u, cadenceDecision.state.nativeOutwardReleaseSamples);
			cadence.previous = cadenceDecision.state;
			cadence.cadenceRepeat = false;
			cadence.sourceSequence = 1341;
			cadence.retentionSourceSequence = 1341;
			cadence.currentNativeOutwardProof.lastObservedSourceSequence = 1341;
			Assert::IsTrue(EvaluateNearBlackPresentationEpisode(cadence).outwardEntryReleased);

			// Keep the producer's anchored candidate across one detector step
			// while requiring each frame's actual candidate to match its proof.
			auto jitter = input;
			auto jitterDecision = EvaluateNearBlackPresentationEpisode(jitter);
			jitter.previous = jitterDecision.state;
			jitter.sourceSequence = 1340;
			jitter.retentionSourceSequence = 1340;
			jitter.currentNativeOutwardProof.lastObservedSourceSequence = 1340;
			jitter.currentObservation.top += 2;
			jitter.currentObservation.bottom -= 2;
			jitter.currentNativeOutwardProof.candidate = jitter.currentObservation;
			jitter.currentNativeObservation = jitter.currentObservation;
			jitterDecision = EvaluateNearBlackPresentationEpisode(jitter);
			Assert::AreEqual(2u, jitterDecision.state.nativeOutwardReleaseSamples);
			jitter.previous = jitterDecision.state;
			jitter.sourceSequence = 1341;
			jitter.retentionSourceSequence = 1341;
			jitter.currentNativeOutwardProof.lastObservedSourceSequence = 1341;
			Assert::IsTrue(EvaluateNearBlackPresentationEpisode(jitter).outwardEntryReleased);
			auto reject = [](const NearBlackPresentationEpisodeInput& changed)
			{
				Assert::IsFalse(QualifiesObsoleteNearBlackEntryNativeOutwardPictureSample(changed));
				Assert::IsFalse(EvaluateNearBlackPresentationEpisode(changed).outwardEntryReleased);
			};
			auto changed = input;
			changed.globalNearBlack = true;
			reject(changed);
			changed = input;
			changed.currentNativeOutwardProof.confirmations = 0;
			reject(changed);
			changed = input;
			changed.currentNativeOutwardProof.lastObservedSourceSequence--;
			reject(changed);
			changed = input;
			changed.currentNativeOutwardProof.firstPictureSourceSequence = 0;
			reject(changed);
			changed = input;
			changed.currentNativeOutwardProof.sourceGeneration++;
			reject(changed);
			changed = input;
			changed.currentNativeOutwardProofPresentationEpoch++;
			reject(changed);
			changed = input;
			changed.currentNativeOutwardProof.base.top += 2;
			reject(changed);
			changed = input;
			changed.currentNativeOutwardProof.candidate.bottom -= 2;
			reject(changed);
			changed = input;
			changed.currentObservationOrigin = ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
			reject(changed);
			changed = input;
			changed.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
			reject(changed);
			changed = input;
			changed.currentNativeObservation.top += 2;
			reject(changed);
			changed = input;
			changed.retentionSafe = true;
			reject(changed);
			changed = input;
			changed.retentionSourceSequence--;
			reject(changed);
			changed = input;
			changed.boundedVisibleContentOutsideCrop = false;
			reject(changed);
			changed = input;
			changed.sceneBoundary = true;
			reject(changed);
			changed = input;
			changed.cadenceRepeat = true;
			reject(changed);
		}
		TEST_METHOD(NearBlackCropRevalidationRejectsUnrelatedOrStaleAuthority)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			ActivePictureBounds unrelated = scope;
			unrelated.top = 68;
			unrelated.bottom = 2092;
			unrelated.aspectRatio = 1.8972;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 47;
			input.previous.presentationEpoch = 53;
			input.previous.entryTrustedCropAvailable = true;
			input.previous.entryTrustedCrop = scope;
			input.previous.fullRasterStartedSourceSequence = 3000;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.globalNearBlack = false;
			input.trustedCropAvailable = true;
			input.trustedCrop = scope;
			input.currentObservationAvailable = true;
			input.currentObservation = scope;
			input.retentionEvaluated = true;
			input.retentionSafe = true;
			input.retentionBounds = scope;
			input.knownTrustedGeometryReacquired = true;
			input.reacquiredTrustedGeometry = unrelated;
			input.reacquiredTrustedClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.reacquiredSourceGeneration = 47;
			input.reacquiredSourceSequence = 3000;
			input.reacquiredPresentationEpoch = 53;
			input.reacquisitionIsCurrentAssociation = true;
			input.presentationEpoch = 53;
			input.sourceGeneration = 47;
			input.retentionSourceGeneration = input.sourceGeneration;
			input.sourceSequence = 3000;
			input.retentionSourceSequence = 3000;

			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.revalidationSamples);

			input.previous = decision.state;
			input.reacquiredTrustedGeometry = scope;
			input.reacquiredPresentationEpoch = 52;
			input.sourceSequence = 3001;
			input.retentionSourceSequence = 3001;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.revalidationSamples);

			input.previous = decision.state;
			input.reacquiredPresentationEpoch = 53;
			input.reacquisitionIsCurrentAssociation = false;
			input.sourceSequence = 3002;
			input.retentionSourceSequence = 3002;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.revalidationSamples);
		}

		TEST_METHOD(NearBlackCropRevalidationRejectsPreLatchProofAcrossLongSafeGap)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 67;
			input.previous.presentationEpoch = 71;
			input.previous.entryTrustedCropAvailable = true;
			input.previous.entryTrustedCrop = scope;
			input.previous.fullRasterStartedSourceSequence = 5000;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.currentObservationAvailable = true;
			input.currentObservation = scope;
			input.retentionEvaluated = true;
			input.retentionSafe = true;
			input.retentionBounds = scope;
			input.knownTrustedGeometryReacquired = true;
			input.reacquiredTrustedGeometry = scope;
			input.reacquiredTrustedClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.reacquiredSourceGeneration = 67;
			input.reacquiredSourceSequence = 4999;
			input.reacquiredPresentationEpoch = 71;
			input.reacquisitionIsCurrentAssociation = true;
			input.presentationEpoch = 71;
			input.sourceGeneration = 67;
			input.retentionSourceGeneration = input.sourceGeneration;
			input.framesPerSecond = 23.976;

			auto decision = EvaluateNearBlackPresentationEpisode(input);
			for (uint64_t sequence = 5001; sequence <= 5030; ++sequence)
			{
				Assert::IsFalse(decision.releasedToTrustedCrop);
				Assert::AreEqual(0u, decision.state.revalidationSamples);
				Assert::AreEqual(static_cast<int>(
					NearBlackPresentationMode::FULL_RASTER),
					static_cast<int>(decision.state.mode));
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
			}

			input.reacquiredSourceSequence = 5031;
			for (uint64_t sequence = 5031; sequence <= 5037; ++sequence)
			{
				input.previous = decision.state;
				input.sourceSequence = sequence;
				input.retentionSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				if (sequence < 5037)
					Assert::IsFalse(decision.releasedToTrustedCrop);
			}
			Assert::IsTrue(decision.releasedToTrustedCrop);
		}

		TEST_METHOD(NearBlackCropRevalidationNeedsDistinctFramesAndFullRasterWins)
		{
			const ActivePictureBounds scope = TrustedScopeCrop().geometry;
			NearBlackPresentationEpisodeInput input;
			input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
			input.previous.sourceGeneration = 59;
			input.previous.presentationEpoch = 61;
			input.previous.entryTrustedCropAvailable = true;
			input.previous.entryTrustedCrop = scope;
			input.previous.fullRasterStartedSourceSequence = 4000;
			input.measurementCurrent = true;
			input.nearBlackEvaluated = true;
			input.currentObservationAvailable = true;
			input.currentObservation = scope;
			input.retentionEvaluated = true;
			input.retentionSafe = true;
			input.retentionBounds = scope;
			input.knownTrustedGeometryReacquired = true;
			input.reacquiredTrustedGeometry = scope;
			input.reacquiredTrustedClassification =
				ActivePictureClassification::BAR_CROP_TRUSTED;
			input.reacquiredSourceGeneration = 59;
			input.reacquiredSourceSequence = 4000;
			input.reacquiredPresentationEpoch = 61;
			input.reacquisitionIsCurrentAssociation = true;
			input.presentationEpoch = 61;
			input.sourceGeneration = 59;
			input.retentionSourceGeneration = input.sourceGeneration;
			input.sourceSequence = 4000;
			input.retentionSourceSequence = 4000;
			input.cadenceRepeat = true;

			auto decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.revalidationSamples);

			input.previous = decision.state;
			input.cadenceRepeat = false;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u, decision.state.revalidationSamples);

			input.previous = decision.state;
			input.cadenceRepeat = true;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(1u, decision.state.revalidationSamples);

			input.previous = decision.state;
			input.cadenceRepeat = false;
			input.sourceSequence = 4001;
			input.retentionSourceSequence = 4001;
			input.globalNearBlack = true;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(0u, decision.state.revalidationSamples);

			input.previous = decision.state;
			input.globalNearBlack = false;
			input.fullRasterAuthorityAvailable = true;
			input.sourceSequence = 4002;
			input.retentionSourceSequence = 4002;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::IsTrue(decision.ended);
			Assert::IsFalse(decision.releasedToTrustedCrop);
			Assert::AreEqual(static_cast<int>(
				NearBlackPresentationMode::INACTIVE),
				static_cast<int>(decision.state.mode));
		}

		TEST_METHOD(RetainedNearBlackEpisodeYieldsAfterFreshSafeScopeReturns)
		{
			auto input = ReaffirmedRetainedScope();
			NearBlackPresentationEpisodeDecision decision;
			for (uint64_t sequence = 1963; sequence <= 1969; ++sequence)
			{
				input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = sequence;
				decision = EvaluateNearBlackPresentationEpisode(input);
				Assert::IsFalse(decision.changedToFullRaster);
				if (sequence < 1969)
					Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP), int(decision.state.mode));
				input.previous = decision.state;
			}
			Assert::IsTrue(decision.ended, L"Bright, independently safe scope must end the stale title episode");
			Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE), int(decision.state.mode));
			// Subsequent bounded content must pass through ordinary presentation
			// arbitration rather than the old title episode's full-raster latch.
			input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = 2014;
			input.boundedVisibleContentOutsideCrop = true;
			input.retentionSafe = input.retentionExcludedBandsPixelSafe = false;
			input.currentObservation.top = 168;
			decision = EvaluateNearBlackPresentationEpisode(input);
			Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE), int(decision.state.mode));
			Assert::IsFalse(decision.changedToFullRaster);
		}

		TEST_METHOD(RetainedNearBlackEpisodeRequiresFreshCurrentSafeScope)
		{
			for (int invalid = 0; invalid < 7; ++invalid)
			{
				auto input = ReaffirmedRetainedScope();
				for (uint64_t sequence = 1963; sequence <= 1980; ++sequence)
				{
					input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = sequence;
					if (invalid == 0) input.cadenceRepeat = true;
					if (invalid == 1) input.retentionSourceSequence = sequence - 1;
					if (invalid == 2) input.retentionSafe = input.retentionExcludedBandsPixelSafe = false;
					if (invalid == 3) input.globalNearBlack = true;
					if (invalid == 4) input.currentObservationAvailable = false;
					if (invalid == 5) input.reacquiredPresentationEpoch = 9;
					if (invalid == 6) input.reacquisitionIsCurrentAssociation = false;
					const auto decision = EvaluateNearBlackPresentationEpisode(input);
					Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP), int(decision.state.mode));
					Assert::IsFalse(decision.ended);
					input.previous = decision.state;
				}
			}
		}
		TEST_METHOD(RetainedEpisodeHandoffPreservesCropThroughProvisionalFrameAndRecovery)
		{
			auto input = ReaffirmedRetainedScope();
			NearBlackPresentationEpisodeDecision episode;
			for (uint64_t sequence = 1963; sequence <= 1969; ++sequence)
			{
				input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = sequence;
				episode = EvaluateNearBlackPresentationEpisode(input);
				input.previous = episode.state;
			}
			Assert::IsTrue(episode.releasedToTrustedCrop);
			Input crop = TrustedScopeCrop();
			crop.geometry = input.trustedCrop;
			crop.geometrySourceGeneration = crop.frameSourceGeneration = input.sourceGeneration;
			crop.frameSourceSequence = input.sourceSequence;
			// Analysis precedes episode retirement in the renderer: this frame's
			// observation is still provisional, despite independent pixel proof.
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsProvisional = true;
			crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
			crop.frameLocalPresentationRetentionEvaluated = input.retentionEvaluated;
			crop.frameLocalPresentationRetentionSafe = input.retentionSafe;
			crop.nearBlackEpisodeRetainCrop = episode.state.mode == NearBlackPresentationMode::RETAIN_CROP;
			crop.nearBlackEpisodeFullRaster = episode.state.mode == NearBlackPresentationMode::FULL_RASTER;
			const auto candidate = Evaluate(crop);
			Assert::AreEqual(int(DecisionOwner::PIXEL_SAFE_RETENTION), int(candidate.owner));
			for (bool recoveryActive : { false, true })
			{
				PresentationRecoveryInput recovery;
				recovery.crop = crop;
				recovery.candidate = candidate;
				recovery.presentationEpoch = input.presentationEpoch;
				recovery.previous.active = recoveryActive;
				recovery.previous.sourceGeneration = input.sourceGeneration;
				recovery.previous.presentationEpoch = input.presentationEpoch;
				recovery.previous.trustedCrop = input.trustedCrop;
				recovery.confirmedPresentationResolved = episode.releasedToTrustedCrop;
				const auto handedOff = EvaluatePresentationRecovery(recovery);
				Assert::IsFalse(handedOff.state.active, L"Completed episode proof must not start another recovery dwell");
				Assert::IsTrue(handedOff.presentation.applyCrop);
				Assert::AreEqual(0, handedOff.presentation.sourceBounds.left);
				Assert::AreEqual(280, handedOff.presentation.sourceBounds.top);
				Assert::AreEqual(3840, handedOff.presentation.sourceBounds.right);
				Assert::AreEqual(1880, handedOff.presentation.sourceBounds.bottom);
			}
			// On the next bright frame normal crop authority resumes, with the
			// same rectangle and no intervening full-raster presentation.
			++crop.frameSourceSequence;
			crop.latestObservationSupportsCrop = true;
			crop.latestObservationIsProvisional = false;
			crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			const auto next = Evaluate(crop);
			Assert::IsTrue(next.applyCrop);
			Assert::AreEqual(int(DecisionOwner::TRUSTED_CROP), int(next.owner));
			Assert::AreEqual(candidate.sourceBounds.left, next.sourceBounds.left);
			Assert::AreEqual(candidate.sourceBounds.top, next.sourceBounds.top);
			Assert::AreEqual(candidate.sourceBounds.right, next.sourceBounds.right);
			Assert::AreEqual(candidate.sourceBounds.bottom, next.sourceBounds.bottom);
		}

        TEST_METHOD(NearBlackReleasedCropSurvivesRendererInspectionHandoff)
        {
            for (bool fullRasterEntry : { false, true })
            {
                auto input = ReaffirmedRetainedScope(fullRasterEntry);
                NearBlackPresentationEpisodeDecision episode;
                for (uint64_t sequence = 1963; sequence <= 1969; ++sequence)
                {
                    input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = sequence;
                    episode = EvaluateNearBlackPresentationEpisode(input);
                    input.previous = episode.state;
                }
                Assert::IsTrue(episode.releasedToTrustedCrop);
                auto crop = TrustedScopeCrop();
                crop.geometry = input.trustedCrop;
                crop.geometrySourceGeneration = crop.frameSourceGeneration = input.sourceGeneration;
                crop.frameSourceSequence = input.sourceSequence;
                crop.latestObservationSupportsCrop = false;
                crop.latestObservationIsProvisional = true;
                crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
                crop.frameLocalPresentationRetentionEvaluated = crop.frameLocalPresentationRetentionSafe = true;
                // Use the same ownership adapter as renderer arbitration;
                // the obsolete inspection latch is still present on release.
                crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(episode);
                crop.nearBlackEpisodeFullRaster = episode.state.mode == NearBlackPresentationMode::FULL_RASTER;
                crop.presentationFailOpen = !crop.nearBlackEpisodeRetainCrop;
                PresentationRecoveryInput recovery;
                recovery.crop = crop;
                recovery.candidate = Evaluate(crop);
                recovery.presentationEpoch = input.presentationEpoch;
                recovery.previous.active = true;
                recovery.previous.sourceGeneration = input.sourceGeneration;
                recovery.previous.presentationEpoch = input.presentationEpoch;
                recovery.previous.trustedCrop = input.trustedCrop;
                recovery.confirmedPresentationResolved = episode.releasedToTrustedCrop;
                const auto handoff = EvaluatePresentationRecovery(recovery);
                Assert::IsFalse(handoff.state.active);
                Assert::IsTrue(handoff.presentation.applyCrop,
                    L"Completed current pixel-safe proof must own the crop on its release frame.");
                Assert::AreEqual(280, handoff.presentation.sourceBounds.top);
                Assert::AreEqual(1880, handoff.presentation.sourceBounds.bottom);
            }
        }

        TEST_METHOD(NearBlackReleaseRetiresPriorInspectionLatchForFollowingFrame)
        {
            auto input = ReaffirmedRetainedScope();
            NearBlackPresentationEpisodeDecision episode;
            for (uint64_t sequence = 1963; sequence <= 1969; ++sequence)
            {
                input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = sequence;
                episode = EvaluateNearBlackPresentationEpisode(input);
                input.previous = episode.state;
            }
            Assert::IsTrue(episode.releasedToTrustedCrop);
            VerticalInspectionBridgeInput inspection;
            inspection.previous.active = inspection.previous.failOpenLatched = true;
            inspection.previous.sourceGeneration = inspection.sourceGeneration = input.sourceGeneration;
            inspection.previous.presentationEpoch = inspection.presentationEpoch = input.presentationEpoch;
            inspection.previous.trustedBase = inspection.trustedBase = input.trustedCrop;
            inspection.previous.firstCandidateSourceSequence = 1900;
            inspection.sourceSequence = input.sourceSequence;
            inspection.candidate = inspection.retentionRequested = true;
            // Existing wiring: neither sampling nor generic contained-recovery
            // retirement applies to this completed near-black episode.
            inspection.containedRetentionResolved = NearBlackEpisodeResolvesInspection(episode);
            const auto retired = UpdateVerticalInspectionBridge(inspection);
            Assert::IsFalse(retired.state.failOpenLatched,
                L"The completed episode must retire the old inspection latch for the next frame.");
            Assert::IsFalse(retired.state.active);
            auto crop = TrustedScopeCrop();
            crop.geometry = input.trustedCrop;
            crop.geometrySourceGeneration = crop.frameSourceGeneration = input.sourceGeneration;
            crop.frameSourceSequence = input.sourceSequence + 1;
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationIsProvisional = true;
            crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = crop.frameLocalPresentationRetentionSafe = true;
            crop.presentationFailOpen = retired.state.failOpenLatched;
            const auto next = Evaluate(crop);
            Assert::IsTrue(next.applyCrop);
            Assert::AreEqual(280, next.sourceBounds.top);
            Assert::AreEqual(1880, next.sourceBounds.bottom);
        }

        TEST_METHOD(NearBlackRetainedEpisodeCannotCarryProofAcrossViewportEpoch)
        {
            for (int variant = 0; variant < 4; ++variant)
            {
                auto input = ReaffirmedRetainedScope();
                const auto oldCrop = input.trustedCrop;
                ++input.presentationEpoch;
                ++input.sourceSequence;
                if (variant == 1)
                {
                    input.trustedCrop.top += 4;
                    input.trustedCrop.bottom -= 4;
                    input.trustedCrop.aspectRatio = 3840.0 /
                        (input.trustedCrop.bottom - input.trustedCrop.top);
                }
                if (variant == 2) input.trustedCropAvailable = false;
                if (variant == 3) input.globalNearBlack = true;
                const auto decision = EvaluateNearBlackPresentationEpisode(input);
                Assert::IsTrue(decision.ended, L"Old presentation proof cannot own the new viewport.");
                Assert::IsFalse(decision.releasedToTrustedCrop);
                Assert::IsFalse(decision.resetTrustedGeometry,
                    L"Resetting presentation proof must not discard valid source crop geometry.");
                Assert::AreEqual(0u, decision.state.revalidationSamples);
                if (variant == 3)
                {
                    Assert::IsTrue(decision.started);
                    Assert::AreEqual(input.presentationEpoch, decision.state.presentationEpoch);
                    Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP), int(decision.state.mode));
                    Assert::AreEqual(oldCrop.top, decision.state.entryTrustedCrop.top);
                }
                else
                    Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE), int(decision.state.mode));
            }
        }
		TEST_METHOD(RetainedEpisodePartialProofRejectsDuplicatesAndRestartsAfterInterruption)
		{
			for (bool sequenceGap : { false, true })
			{
				auto input = ReaffirmedRetainedScope();
				for (uint64_t sequence = 1963; sequence <= 1965; ++sequence)
				{
					input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = sequence;
					input.previous = EvaluateNearBlackPresentationEpisode(input).state;
				}
				Assert::AreEqual(3u, input.previous.revalidationSamples);
				const auto duplicate = EvaluateNearBlackPresentationEpisode(input);
				Assert::AreEqual(3u, duplicate.state.revalidationSamples);
				Assert::IsFalse(duplicate.ended);
				input.previous = duplicate.state;
				if (!sequenceGap)
				{
					input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = 1966;
					input.currentObservationAvailable = false;
					input.previous = EvaluateNearBlackPresentationEpisode(input).state;
					Assert::AreEqual(0u, input.previous.revalidationSamples);
					input.currentObservationAvailable = true;
				}
				// A missed frame or invalid observation both require fresh proof.
				for (uint64_t sequence = 1967; sequence <= 1973; ++sequence)
				{
					input.sourceSequence = input.retentionSourceSequence = input.reacquiredSourceSequence = sequence;
					const auto decision = EvaluateNearBlackPresentationEpisode(input);
					Assert::AreEqual(sequence == 1973, decision.ended);
					if (sequence < 1973)
						Assert::AreEqual(static_cast<unsigned int>(sequence - 1966), decision.state.revalidationSamples);
					input.previous = decision.state;
				}
			}
		}

		TEST_METHOD(NearBlackEpisodePresentationOverridesTransientCropArbitration)
		{
			Input crop = TrustedScopeCrop();
			crop.latestObservationSupportsCrop = false;
			crop.latestObservationIsProvisional = true;
			crop.nearBlackEpisodeRetainCrop = true;
			Decision decision = Evaluate(crop);
			Assert::IsTrue(decision.applyCrop);
			Assert::AreEqual(static_cast<int>(
				DecisionOwner::NEAR_BLACK_EPISODE),
				static_cast<int>(decision.owner));

			crop.nearBlackEpisodeRetainCrop = false;
			crop.nearBlackEpisodeFullRaster = true;
			decision = Evaluate(crop);
			AssertFullRaster(decision);
			Assert::IsTrue(decision.reason.find("near-black title episode") !=
				std::string::npos);
		}
	};
	TEST_CLASS(SamplingRefinementOutwardFitTests)
	{
		static SamplingRefinementOutwardFitInput Current()
		{
			SamplingRefinementOutwardFitInput input;
			input.base = {0,44,3840,2116,3840,2160,1.85328,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			input.rawBounds = input.visibleBase = input.base;
			input.visibleBounds = input.outwardBounds = input.base;
			input.visibleBounds.top = input.outwardBounds.top = 28;
			input.visibleBounds.bottom = input.outwardBounds.bottom = 2132;
			input.baseClassification = input.rawClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.frameGeneration = input.baseGeneration = input.visibleGeneration = 7;
			input.frameSequence = input.rawSequence = input.visibleSequence = 20520;
			input.rawAvailable = input.visibleAvailable = input.outwardAvailable = true;
			input.nearBlackEvaluated = true;
			return input;
		}
	public:
		TEST_METHOD(SamplingRefinementOutwardFitStopsBothEdgeJitterWithoutPromotingCrop)
		{
			for (const int envelopeTop : {26,28})
			for (const int rawTop : {40,44,48})
			for (const int rawBottom : {2112,2116,2120})
			{
				auto input = Current();
				input.rawBounds.top = rawTop; input.rawBounds.bottom = rawBottom;
				input.visibleBounds.top = input.outwardBounds.top = envelopeTop;
				input.visibleBounds.bottom = input.outwardBounds.bottom = 2160-envelopeTop;
				const bool exact = rawTop == 44 && rawBottom == 2116;
				VerticalBarPresentationResolutionInput resolution;
				resolution.genericUpperExpansion = resolution.genericLowerExpansion = true;
				resolution.genericVerticalFitConfirmed = true;
				resolution.genericVerticalFitAuthoritative = exact || CanUseSamplingRefinementOutwardFit(input);
				resolution.genericUpperBound = envelopeTop; resolution.genericLowerBound = 2160-envelopeTop;
				resolution.authoritativeTop = 44; resolution.authoritativeBottom = 2116; resolution.rasterHeight = 2160;
				const auto routing = ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(resolution));
				Assert::IsTrue(routing.fitActive);
				auto crop = TrustedScopeCrop(); crop.geometry = input.base;
				crop.latestObservationSupportsCrop = exact;
				crop.barCropRefinementPending = !exact;
				crop.outwardPresentationActive = routing.fitActive;
				crop.outwardExpansionAvailable = true; crop.outwardExpansion = input.outwardBounds;
				crop.outwardExpansionSourceGeneration = 7;
				const auto decision = Evaluate(crop);
				Assert::AreEqual(envelopeTop, decision.sourceBounds.top);
				Assert::AreEqual(2160-envelopeTop, decision.sourceBounds.bottom);
				Assert::AreEqual(44, crop.geometry.top);
				Assert::AreEqual(exact, crop.latestObservationSupportsCrop);
			}
		}
		TEST_METHOD(SamplingRefinementOutwardFitAllowsOddMeasuredPixelsWithinAlignedEnvelope)
		{
			auto input = Current();
			input.rawBounds.top = 40;
			input.visibleBounds.bottom = 2131; // Actual source pixels before chroma alignment.
			Assert::IsTrue(CanUseSamplingRefinementOutwardFit(input));
		}
		TEST_METHOD(SamplingRefinementOutwardFitRejectsStaleAndMismatchedEvidence)
		{
			for (int failure=0; failure<14; ++failure)
			{
				auto input = Current();
				switch(failure) {
				case 0: ++input.baseGeneration; break;
				case 1: ++input.visibleGeneration; break;
				case 2: --input.rawSequence; break;
				case 3: --input.visibleSequence; break;
				case 4: input.rawAvailable=false; break;
				case 5: input.visibleAvailable=false; break;
				case 6: input.outwardAvailable=false; break;
				case 7: input.visibleBase.top+=2; break;
				case 8: input.rawBounds.top-=6; break;
				case 9: input.rawBounds.bottom+=6; break;
				case 10: input.rawBounds.left+=2; break;
				case 11: input.rawBounds.rasterWidth=1920; break;
				case 12: input.frameGeneration=input.baseGeneration=input.visibleGeneration=0; break;
				case 13: input.frameSequence=input.rawSequence=input.visibleSequence=0; break;
				}
				Assert::IsFalse(CanUseSamplingRefinementOutwardFit(input));
			}
		}
		TEST_METHOD(SamplingRefinementOutwardFitPreservesOwnerAndEnvelopeVetoes)
		{
			for (int failure=0; failure<18; ++failure)
			{
				auto input = Current();
				switch(failure) {
				case 0: input.nearBlackEvaluated=false; break;
				case 1: input.nearBlack=true; break;
				case 2: input.moving=true; break;
				case 3: input.competingOwner=true; break;
				case 4: input.denseArbitrationEnabled=true; break;
				case 5: input.rawClassification=ActivePictureClassification::UNAVAILABLE; break;
				case 6: input.baseClassification=ActivePictureClassification::UNAVAILABLE; break;
				case 7: input.rawOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN; break;
				case 8: input.baseOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN; break;
				case 9: input.outwardBounds.top=29; break;
				case 10: input.outwardBounds.top=30; break;
				case 11: input.visibleBounds.bottom=2116; break;
				case 12: input.visibleBounds.top=44; break;
				case 13: input.outwardBounds.bottom=2116; break;
				case 14: input.outwardBounds.top=-2; break;
				case 15: input.rawBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE; break;
				case 16: input.outwardBounds.left=2; break;
				case 17: input.rawClassification=ActivePictureClassification::PROVISIONAL; break;
				}
				Assert::IsFalse(CanUseSamplingRefinementOutwardFit(input));
			}
		}
	};

	TEST_CLASS(ApprovedGenericFitHoldTests)
	{
		static ApprovedGenericFitHoldInput Current()
		{
			ApprovedGenericFitHoldInput input;
			input.base = {0,44,3840,2116,3840,2160,1.85328,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			input.rawBounds = input.retentionBase = input.base;
			input.baseClassification = input.rawClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
			input.sourceGeneration = input.baseGeneration = input.retentionGeneration = 7;
			input.sourceSequence = input.rawSequence = input.retentionSequence = 100;
			input.presentationEpoch = 9; input.currentTick = 1000; input.holdMs = 500;
			input.rawAvailable = input.retentionValid = input.excludedBandsSafe = input.nearBlackEvaluated = true;
			return input;
		}
		static ActivePictureBounds Envelope(const ApprovedGenericFitHoldInput& input)
		{
			auto bounds = input.base; bounds.top = 28; bounds.bottom = 2132;
			return bounds;
		}
		static void Advance(ApprovedGenericFitHoldInput& input, uint64_t tick)
		{
			++input.sourceSequence; input.rawSequence = input.retentionSequence = input.sourceSequence;
			input.currentTick = tick;
		}
	public:
		TEST_METHOD(ApprovedGenericFitHoldBridgesDropoutsWithoutRenewingAdmission)
		{
			auto input = Current(); auto envelope = Envelope(input);
			input.previous = RecordApprovedGenericFitHold(input,envelope,envelope,true,true);
			Assert::IsTrue(input.previous.available);
			input.currentTick=1050;
			auto duplicate=RecordApprovedGenericFitHold(input,envelope,envelope,true,true);
			Assert::AreEqual(1000ULL,duplicate.admittedTick);
			for (const uint64_t tick : {1100ULL,1200ULL,1500ULL})
			{
				Advance(input,tick); input.rawBounds.top = 40;
				auto result = ResolveApprovedGenericFitHold(input);
				Assert::IsTrue(result.active); Assert::AreEqual(28,result.bounds.top);
				Assert::AreEqual(1000ULL,result.state.admittedTick);
				input.previous = result.state;
			}
			Advance(input,1501); auto expired = ResolveApprovedGenericFitHold(input);
			Assert::IsFalse(expired.active); Assert::IsFalse(expired.state.available);
			input.previous = expired.state; input.currentTick = 1200;
			Assert::IsFalse(ResolveApprovedGenericFitHold(input).active);
		}
		TEST_METHOD(ApprovedGenericFitHoldRecordsOnlyMatchingFinalGenericAdmission)
		{
			auto input = Current(); auto envelope = Envelope(input);
			Assert::IsFalse(RecordApprovedGenericFitHold(input,envelope,envelope,false,true).available);
			Assert::IsFalse(RecordApprovedGenericFitHold(input,envelope,envelope,true,false).available);
			auto other = envelope; other.top=26;
			Assert::IsFalse(RecordApprovedGenericFitHold(input,envelope,other,true,true).available);
			other = envelope; other.bottom=input.base.bottom;
			Assert::IsFalse(RecordApprovedGenericFitHold(input,other,other,true,true).available);
			input.previous=RecordApprovedGenericFitHold(input,envelope,envelope,true,true);
			Assert::IsTrue(input.previous.available);
			Advance(input,1100); envelope.top=30; envelope.bottom=2130;
			auto replaced=RecordApprovedGenericFitHold(input,envelope,envelope,true,true);
			Assert::AreEqual(30,replaced.bounds.top); Assert::AreEqual(1100ULL,replaced.admittedTick);
		}
		TEST_METHOD(ApprovedGenericFitHoldRequiresCoveredCurrentPixelsOrSafeBands)
		{
			auto input=Current(); auto envelope=Envelope(input);
			input.previous=RecordApprovedGenericFitHold(input,envelope,envelope,true,true); Advance(input,1100);
			input.excludedBandsSafe=false;
			Assert::IsFalse(ResolveApprovedGenericFitHold(input).active);
			input.visibleAvailable=true; input.visibleBounds=envelope; input.visibleBounds.bottom=2131;
			Assert::IsTrue(ResolveApprovedGenericFitHold(input).active);
			input.visibleBounds.top=0; input.excludedBandsSafe=true;
			Assert::IsFalse(ResolveApprovedGenericFitHold(input).active);
		}
		TEST_METHOD(ApprovedGenericFitHoldAcceptsInitialZeroPresentationEpoch)
		{
			auto input=Current(); input.presentationEpoch=0; auto envelope=Envelope(input);
			input.previous=RecordApprovedGenericFitHold(input,envelope,envelope,true,true);
			Assert::IsTrue(input.previous.available);
			Advance(input,1100);
			Assert::IsTrue(ResolveApprovedGenericFitHold(input).active);
			input.presentationEpoch=1;
			Assert::IsFalse(ResolveApprovedGenericFitHold(input).active);
		}
		TEST_METHOD(ApprovedGenericFitHoldRetiresOnContextAndOwnerChanges)
		{
			for(int failure=0;failure<16;++failure)
			{
				auto input=Current(); auto envelope=Envelope(input);
				input.previous=RecordApprovedGenericFitHold(input,envelope,envelope,true,true); Advance(input,1100);
				switch(failure) {
				case 0: ++input.sourceGeneration; break;
				case 1: ++input.presentationEpoch; break;
				case 2: input.base.top+=2; break;
				case 3: --input.rawSequence; break;
				case 4: --input.retentionSequence; break;
				case 5: input.nearBlack=true; break;
				case 6: input.moving=true; break;
				case 7: input.competingOwner=true; break;
				case 8: input.denseArbitrationEnabled=true; break;
				case 9: input.rawBounds.top-=6; break;
				case 10: input.rawClassification=ActivePictureClassification::PROVISIONAL; break;
				case 11: input.baseOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN; break;
				case 12: input.retentionValid=false; break;
				case 13: input.currentTick=999; break;
				case 14: input.holdMs=0; break;
				case 15: input.rawBounds.left=2; break;
				}
				auto result=ResolveApprovedGenericFitHold(input);
				Assert::IsFalse(result.active); Assert::IsFalse(result.state.available);
			}
		}
	};

    TEST_CLASS(DenseFitExtentLifetimeTests)
    {
        static VerticalBarPresentationUpdateInput Start(uint64_t hold=2000)
        {
            VerticalBarPresentationUpdateInput input;
            input.current.action=VerticalBarPresentationAction::FIT;
            input.upperContent=input.lowerContent=true;
            input.upperContentTop=80; input.lowerContentBottom=2080;
            input.currentTick=1000; input.currentSourceSequence=10;
            input.holdMs=hold; input.translationEnabled=true;
            return input;
        }
    public:
        TEST_METHOD(DenseFitExtentsExpireIndependentlyDespiteContinuousOtherContent)
        {
            for(int shape=0;shape<4;++shape)
            {
                auto input=Start(); auto state=UpdateVerticalBarPresentation(input);
                Assert::AreEqual(80,state.detectedTop); Assert::AreEqual(2080,state.detectedBottom);
                input.previousOwnsCurrentAnalysis=true;
                if(shape==0)input.upperContentTop=104;
                if(shape==1)input.lowerContentBottom=2056;
                if(shape==2)input.upperContent=false;
                if(shape==3)input.lowerContent=false;
                for(uint64_t tick=1100;tick<=3000;tick+=100)
                {
                    input.previous=state; input.currentTick=tick; ++input.currentSourceSequence;
                    state=UpdateVerticalBarPresentation(input);
                    Assert::AreEqual(80,state.detectedTop); Assert::AreEqual(2080,state.detectedBottom);
                }
                input.previous=state; input.currentTick=3100; ++input.currentSourceSequence;
                state=UpdateVerticalBarPresentation(input);
                const auto message=std::wstring(L"Dense FIT extent expiration case ")+std::to_wstring(shape);
                Assert::AreEqual(shape==0?104:shape==2?0:80,state.detectedTop,message.c_str());
                Assert::AreEqual(shape==1?2056:shape==3?0:2080,state.detectedBottom,message.c_str());
                Assert::IsTrue(state.action==VerticalBarPresentationAction::FIT);
                Assert::AreEqual(3100ULL,state.lastDetectionTick);
                Assert::IsTrue(IsVerticalBarPresentationActive(state,3100,2000,input.currentSourceSequence));
            }
        }
        TEST_METHOD(DenseFitZeroHoldUsesCurrentExtentsWhileTranslationRetentionIsUnchanged)
        {
            auto input=Start(0); input.previous=UpdateVerticalBarPresentation(input);
            input.previousOwnsCurrentAnalysis=true;
            input.upperContentTop=104; input.lowerContentBottom=2056;
            input.currentTick=1100; ++input.currentSourceSequence;
            const auto fit=UpdateVerticalBarPresentation(input);
            Assert::AreEqual(104,fit.detectedTop); Assert::AreEqual(2056,fit.detectedBottom);

            auto translate=Start(); translate.current.action=VerticalBarPresentationAction::TRANSLATE;
            translate.current.translationPixels=-80.0f; translate.lowerContent=false;
            translate.previous=UpdateVerticalBarPresentation(translate);
            translate.previousOwnsCurrentAnalysis=true;
            translate.currentTick=1100; ++translate.currentSourceSequence;
            translate.current.translationPixels=-60.0f; translate.upperContentTop=104;
            const auto held=UpdateVerticalBarPresentation(translate);
            Assert::IsTrue(held.action==VerticalBarPresentationAction::TRANSLATE);
            Assert::AreEqual(-80.0f,held.translationPixels); Assert::AreEqual(80,held.detectedTop);
        }
        TEST_METHOD(DenseFitStaleSamplesCannotResurrectExpiredOrBackwardPresentation)
        {
            auto original=Start(); const auto prior=UpdateVerticalBarPresentation(original);
            for(int failure=0;failure<4;++failure)
            {
                auto input=original; input.previous=prior;
                input.previousOwnsCurrentAnalysis=true;
                input.currentTick=failure==3?999:failure==0?1100:3101;
                if(failure<2)--input.currentSourceSequence;
                const auto result=UpdateVerticalBarPresentation(input);
                Assert::IsTrue(result.action==VerticalBarPresentationAction::NONE,
                    L"An expired duplicate, backward source sequence, or backward clock cannot revive previous FIT geometry.");
                Assert::AreEqual(0ULL,result.lastDetectionTick);
            }
        }
        TEST_METHOD(DenseFitDuplicateSamplesCannotRefreshButNewFartherPixelsApplyImmediately)
        {
            auto input=Start(); input.previous=UpdateVerticalBarPresentation(input);
            input.previousOwnsCurrentAnalysis=true;
            input.currentTick=1100; input.upperContentTop=104;
            const auto duplicate=UpdateVerticalBarPresentation(input);
            Assert::AreEqual(1000ULL,duplicate.lastDetectionTick,
                L"Revisiting the same dense source sample cannot renew a FIT lifetime.");
            Assert::AreEqual(80,duplicate.detectedTop);
            input.previous=duplicate; ++input.currentSourceSequence;
            input.upperContentTop=60; input.lowerContentBottom=2090;
            const auto expanded=UpdateVerticalBarPresentation(input);
            Assert::AreEqual(60,expanded.detectedTop); Assert::AreEqual(2090,expanded.detectedBottom);
            Assert::AreEqual(1100ULL,expanded.lastDetectionTick);
            Assert::AreEqual(1100ULL,expanded.fitTopExtentTick);
            Assert::AreEqual(1100ULL,expanded.fitBottomExtentTick);
        }
        TEST_METHOD(ScopeCaptionArAnalysisKeepsClassicInspectionActive)
        {
            using namespace AlphaSourceCrop;
            const auto inactive=NearBlackPresentationMode::INACTIVE;
            Assert::IsFalse(SubtitlePreviewOverridesClassicHandling(SubtitlePreviewMode::None));
            Assert::IsTrue(ScopeCaptionArAnalysisEligible(
                true,true,false,false,inactive,true),
                L"Classic Scope fitting needs exact caption and bar measurements between episodes.");
            Assert::IsFalse(ScopeCaptionArAnalysisEligible(
                true,true,false,false,inactive,false),
                L"Ordinary mode Off without fitting does not start a worker.");
            for(const auto episode:{NearBlackPresentationMode::FULL_RASTER,
                NearBlackPresentationMode::RETAIN_CROP})
                Assert::IsTrue(ScopeCaptionArAnalysisEligible(
                    true,true,false,false,episode,false),
                    L"Cold AR bootstrap still measures with fitting disabled.");
            Assert::IsFalse(ScopeCaptionArAnalysisEligible(
                false,true,false,false,inactive,true));
            Assert::IsFalse(ScopeCaptionArAnalysisEligible(
                true,false,false,false,inactive,true));
            Assert::IsFalse(ScopeCaptionArAnalysisEligible(
                true,true,true,false,inactive,true));
            Assert::IsFalse(ScopeCaptionArAnalysisEligible(
                true,true,false,true,inactive,true));
        }
        TEST_METHOD(ColdCaptionArAnalysisRunsWithSubtitleVisualsAndFitDisabled)
        {
            const auto preview = SubtitlePreviewMode::None;
            const bool subtitleFit = false;
            Assert::IsFalse(SubtitlePreviewRequiresFullRaster(preview));
            Assert::IsFalse(SubtitlePreviewOverridesClassicHandling(preview));
            Assert::IsFalse(subtitleFit);
            const auto cold = NearBlackPresentationMode::FULL_RASTER;
            Assert::IsTrue(ColdCaptionArAnalysisEligible(true, true, false, false, cold),
                L"Automatic Scope crop must measure current caption/bar evidence even when preview and fit are off.");
            Assert::IsFalse(ColdCaptionArAnalysisEligible(false, true, false, false, cold));
            Assert::IsFalse(ColdCaptionArAnalysisEligible(true, false, false, false, cold));
            Assert::IsFalse(ColdCaptionArAnalysisEligible(true, true, true, false, cold));
            Assert::IsFalse(ColdCaptionArAnalysisEligible(true, true, false, true, cold));
            Assert::IsFalse(ColdCaptionArAnalysisEligible(true, true, false, false,
                NearBlackPresentationMode::INACTIVE));
            Assert::IsTrue(ColdCaptionArAnalysisEligible(true, true, false, false,
                NearBlackPresentationMode::RETAIN_CROP),
                L"A retained wide crop still needs current caption/bar analysis to rebase to Scope.");
            Assert::IsTrue(NearBlackBlocksCaptionBackedLogicalAcquisition(true, cold, false));
            Assert::IsFalse(NearBlackBlocksCaptionBackedLogicalAcquisition(true, cold, true),
                L"Only a current strict worker proof releases logical acquisition under darkness.");
        }


        TEST_METHOD(RawDarknessKeepsEpisodeOwnerAcrossEffectiveCaptionProofToggles)
        {
            NearBlackPresentationEpisodeInput input;
            input.sourceGeneration = 1;
            input.presentationEpoch = 2;
            input.framesPerSecond = 23.976;
            const bool effectiveDark[] = { true, false, true, false, true };
            for (unsigned index = 0; index < 5; ++index)
            {
                input.sourceSequence = 1216 + index;
                input.measurementCurrent = true;
                const auto signal = SelectNearBlackEpisodeDarkness(
                    true, true, effectiveDark[index]);
                Assert::IsTrue(signal.evaluated && signal.nearBlack,
                    L"Current raw darkness must own episode lifetime when caption masking toggles.");
                input.nearBlackEvaluated = signal.evaluated;
                input.globalNearBlack = signal.nearBlack;
                const auto decided = EvaluateNearBlackPresentationEpisode(input);
                Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                    int(decided.state.mode),
                    L"Sources with unchanged dark pixels cannot repeatedly retire and restart the episode.");
                input.previous = decided.state;
            }
            const auto unmeasured = SelectNearBlackEpisodeDarkness(
                false, true, false);
            Assert::IsFalse(unmeasured.evaluated,
                L"A stale raw sample cannot acquire a fresh episode vote.");
            const auto genuinelyBright = SelectNearBlackEpisodeDarkness(
                true, false, true);
            Assert::IsTrue(genuinelyBright.evaluated);
            Assert::IsFalse(genuinelyBright.nearBlack,
                L"Effective caption masking cannot keep an episode dark after raw pixels brighten.");
        }

        TEST_METHOD(RetainedWideEpisodeRebasesOnExactCurrentCaptionProof)
        {
            const ActivePictureBounds wide{ 0, 66, 3840, 2092, 3840, 2160,
                3840.0 / 2026.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
            const ActivePictureBounds physical{ 0, 268, 3840, 1892, 3840, 2160,
                3840.0 / 1624.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
            NearBlackPresentationEpisodeInput episode;
            episode.sourceGeneration = 1;
            episode.presentationEpoch = 2;
            episode.sourceSequence = episode.retentionSourceSequence = 428;
            episode.measurementCurrent = episode.nearBlackEvaluated =
                episode.globalNearBlack = episode.trustedCropAvailable = true;
            episode.trustedCrop = wide;
            episode.currentObservationAvailable = episode.retentionEvaluated =
                episode.retentionSafe = episode.retentionExcludedBandsPixelSafe = true;
            episode.currentObservation = episode.retentionBounds = wide;
            episode.currentObservationClassification =
                ActivePictureClassification::PROVISIONAL;
            episode.retentionSourceGeneration = 1;
            episode.framesPerSecond = 23.976;
            auto entry = EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
                int(entry.state.mode), L"The preceding dark source retains its admitted wide crop.");
            episode.previous = entry.state;
            episode.sourceSequence = episode.retentionSourceSequence = 429;
            const auto current = EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
                int(current.state.mode));

            Input crop;
            crop.automaticCropEnabled = true;
            crop.nearBlackEpisodeRetainCrop = true;
            crop.sharedGeometryAvailable = crop.latestObservationSupportsCrop = true;
            crop.latestObservationClassification = crop.classification =
                ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.frameSourceGeneration = crop.geometrySourceGeneration = 1;
            crop.frameSourceSequence = 429;
            crop.framePresentationEpoch = 2;
            crop.rasterWidth = 3840;
            crop.rasterHeight = 2160;
            crop.geometry = physical;
            crop.currentVisibleBoundsAvailable = true;
            crop.currentVisibleBase = physical;
            crop.currentVisibleBounds = { 0, 268, 3840, 2045, 3840, 2160,
                3840.0 / 1777.0, ActivePictureBounds::BarAxes::NONE };
            crop.currentVisibleSourceGeneration = 1;
            crop.currentVisibleSourceSequence = 429;
            ColdMaskedCaptionPresentationInput proposal;
            proposal.crop = &crop;
            proposal.episode = current.state;
            proposal.previousAdmission.available = true;
            proposal.previousAdmission.trustedCrop = wide;
            proposal.previousAdmission.sourceGeneration = 1;
            proposal.previousAdmission.presentationEpoch = 2;
            proposal.strictProofCurrent = true;
            proposal.strictPicture = physical;
            proposal.proofSourceGeneration = proposal.captionSourceGeneration = 1;
            proposal.proofSourceSequence = proposal.captionSourceSequence = 429;
            proposal.proofPresentationEpoch = 2;
            proposal.captionConfirmed = true;
            proposal.captionBounds = { 1382, 1922, 2454, 2042, 3840, 2160,
                1072.0 / 120.0, ActivePictureBounds::BarAxes::NONE };
            proposal.currentObservationAvailable = proposal.currentRetentionEvaluated = true;
            proposal.currentObservation = { 0, 276, 3840, 2010, 3840, 2160,
                3840.0 / 1734.0, ActivePictureBounds::BarAxes::NONE };
            proposal.observationSourceGeneration = proposal.retentionSourceGeneration = 1;
            proposal.observationSourceSequence = proposal.retentionSourceSequence = 429;
            proposal.retentionBase = physical;
            const auto bounded = BuildColdMaskedCaptionPresentation(proposal);
            Assert::IsTrue(bounded.available,
                L"Fresh guarded Scope proof must replace the old retained wide base atomically.");
            Assert::AreEqual(268, bounded.presentation.sourceBounds.top);
            Assert::AreEqual(2046, bounded.presentation.sourceBounds.bottom);
            const auto admitted = AdmitCropPresentation(
                proposal.previousAdmission, crop, bounded.presentation, 2);
            Assert::IsFalse(admitted.blocked);
            Assert::AreEqual(1892, admitted.state.trustedCrop.bottom);
            ColdMaskedCaptionCommitInput commit;
            commit.currentEpisode = current.state;
            commit.currentAdmission = admitted.state;
            commit.action = ColdMaskedCaptionCommitAction::BOUNDED;
            commit.proposalMode = current.state.mode;
            commit.proposalEntryCropAvailable = true;
            commit.proposalEntryCrop = wide;
            commit.proposalStartSequence = current.state.startedSourceSequence;
            commit.sourceGeneration = 1;
            commit.presentationEpoch = 2;
            commit.currentSourceSequence = commit.selectedSourceSequence =
                commit.proofSourceSequence = 429;
            commit.selectedBase = physical;
            commit.selectedEnvelope = bounded.presentation.sourceBounds;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(commit).committed,
                L"A zero-output source may acquire proof but cannot publish an unrendered rebase.");
            commit.currentSourceSequence = 430;
            commit.renderedAndSubmitted = true;
            const auto selected = CommitColdMaskedCaptionPresentation(commit);
            Assert::IsTrue(selected.committed,
                L"The later output selecting source 429 must commit its own physical base and envelope.");
            Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                int(selected.state.mode));
            Assert::AreEqual(268, selected.state.entryTrustedCrop.top);
            Assert::AreEqual(1892, selected.state.entryTrustedCrop.bottom);
            Assert::AreEqual(2046, selected.state.boundedPresentation.bottom);
            Assert::AreEqual(uint64_t(429),
                selected.state.boundedPresentationPresentedSequence);
            auto repeat = commit;
            repeat.currentEpisode = selected.state;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(repeat).committed,
                L"A repeated output cannot rebase or renew the already committed source.");

            // Source 430 has no completed worker result. The selected 429
            // envelope remains pixel-safe for this exact next source.
            crop.frameSourceSequence = crop.currentVisibleSourceSequence = 430;
            crop.nearBlackEpisodeRetainCrop =
                selected.state.mode == NearBlackPresentationMode::RETAIN_CROP;
            crop.nearBlackEpisodeFullRaster =
                selected.state.mode == NearBlackPresentationMode::FULL_RASTER;
            proposal.episode = selected.state;
            proposal.previousAdmission = admitted.state;
            proposal.strictProofCurrent = proposal.captionConfirmed = false;
            proposal.currentObservation = physical;
            proposal.currentRetentionSafe = true;
            proposal.observationSourceSequence = proposal.retentionSourceSequence = 430;
            const auto missingWorker = BuildColdMaskedCaptionPresentation(proposal);
            Assert::IsTrue(missingWorker.available && missingWorker.held,
                L"One worker-late source with fresh safe pixels must not restore the old wide crop.");
            Assert::AreEqual(2046, missingWorker.presentation.sourceBounds.bottom);
            const auto heldAdmission = AdmitCropPresentation(
                admitted.state, crop, missingWorker.presentation, 2);
            Assert::IsFalse(heldAdmission.blocked);
            auto delayed = commit;
            delayed.currentEpisode = missingWorker.episodeState;
            delayed.currentAdmission = heldAdmission.state;
            delayed.proposalMode = NearBlackPresentationMode::FULL_RASTER;
            delayed.proposalStartSequence = 429;
            delayed.currentSourceSequence = 431;
            delayed.selectedSourceSequence = 430;
            delayed.proofSourceSequence = 429;
            delayed.selectedEnvelope = missingWorker.presentation.sourceBounds;
            const auto laterOutput = CommitColdMaskedCaptionPresentation(delayed);
            Assert::IsTrue(laterOutput.committed,
                L"A delayed output selecting the zero-output worker-gap source commits only that source.");
            Assert::AreEqual(uint64_t(430),
                laterOutput.state.boundedPresentationPresentedSequence);
            Assert::AreEqual(2046, laterOutput.state.boundedPresentation.bottom);
            auto wrongEpoch = delayed;
            ++wrongEpoch.presentationEpoch;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(wrongEpoch).committed);
            auto staleAdmission = delayed;
            staleAdmission.currentAdmission.trustedCrop = wide;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(staleAdmission).committed,
                L"A delayed packet cannot commit through an admission for the old wide base.");
            auto domainReset = delayed;
            domainReset.currentEpisode = {};
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(domainReset).committed,
                L"A same-generation analysis-domain reset must reject an old selected snapshot.");
            auto missingSubmit = delayed;
            missingSubmit.renderedAndSubmitted = false;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(missingSubmit).committed);
        }

        TEST_METHOD(OneCleanSourceCannotReleaseAdmittedDarkCaptionEnvelope)
        {
            const ActivePictureBounds physical{ 0, 268, 3840, 1892, 3840, 2160,
                3840.0 / 1624.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
            NearBlackPresentationEpisodeInput episode;
            episode.sourceGeneration = 7;
            episode.presentationEpoch = 0;
            episode.sourceSequence = 100;
            episode.measurementCurrent = episode.nearBlackEvaluated =
                episode.globalNearBlack = true;
            const auto firstEpisode = EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                int(firstEpisode.state.mode));

            Input crop;
            crop.automaticCropEnabled = crop.nearBlackEpisodeFullRaster =
                crop.sharedGeometryAvailable = crop.latestObservationSupportsCrop = true;
            crop.latestObservationClassification = crop.classification =
                ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.frameSourceGeneration = crop.geometrySourceGeneration = 7;
            crop.frameSourceSequence = 100;
            crop.framePresentationEpoch = 0;
            crop.rasterWidth = 3840;
            crop.rasterHeight = 2160;
            crop.geometry = physical;
            crop.currentVisibleBoundsAvailable = true;
            crop.currentVisibleBase = physical;
            crop.currentVisibleBounds = { 0, 268, 3840, 2042, 3840, 2160,
                3840.0 / 1774.0, ActivePictureBounds::BarAxes::NONE };
            crop.currentVisibleSourceGeneration = 7;
            crop.currentVisibleSourceSequence = 100;
            ColdMaskedCaptionPresentationInput proposal;
            proposal.crop = &crop;
            proposal.episode = firstEpisode.state;
            proposal.strictProofCurrent = proposal.captionConfirmed = true;
            proposal.strictPicture = physical;
            proposal.proofSourceGeneration = proposal.captionSourceGeneration = 7;
            proposal.proofSourceSequence = proposal.captionSourceSequence = 100;
            proposal.proofPresentationEpoch = 0;
            proposal.captionBounds = { 1382, 1922, 2454, 2042, 3840, 2160,
                1072.0 / 120.0, ActivePictureBounds::BarAxes::NONE };
            proposal.currentObservationAvailable = proposal.currentRetentionEvaluated = true;
            proposal.currentObservation = { 0, 276, 3840, 2010, 3840, 2160,
                3840.0 / 1734.0, ActivePictureBounds::BarAxes::NONE };
            proposal.observationSourceGeneration = proposal.retentionSourceGeneration = 7;
            proposal.observationSourceSequence = proposal.retentionSourceSequence = 100;
            proposal.retentionBase = physical;
            const auto first = BuildColdMaskedCaptionPresentation(proposal);
            Assert::IsTrue(first.available);
            const auto admitted = AdmitCropPresentation({}, crop, first.presentation, 0);
            Assert::IsFalse(admitted.blocked);
            Assert::AreEqual(2044, admitted.presentation.sourceBounds.bottom);

            ColdMaskedCaptionCommitInput commit;
            commit.currentEpisode = firstEpisode.state;
            commit.currentAdmission = admitted.state;
            commit.action = ColdMaskedCaptionCommitAction::BOUNDED;
            commit.proposalMode = firstEpisode.state.mode;
            commit.proposalStartSequence =
                firstEpisode.state.fullRasterStartedSourceSequence;
            commit.sourceGeneration = 7;
            commit.presentationEpoch = 0;
            commit.currentSourceSequence = commit.selectedSourceSequence =
                commit.proofSourceSequence = 100;
            commit.selectedBase = physical;
            commit.selectedEnvelope = admitted.presentation.sourceBounds;
            commit.renderedAndSubmitted = true;
            const auto presented = CommitColdMaskedCaptionPresentation(commit);
            Assert::IsTrue(presented.committed);
            episode.previous = presented.state;
            episode.sourceSequence = 101;
            const auto nextEpisode = EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                int(nextEpisode.state.mode),
                L"Raw darkness stays true through one clean subtitle measurement.");
            crop.frameSourceSequence = crop.currentVisibleSourceSequence = 101;
            crop.currentVisibleBounds = physical;
            proposal.episode = nextEpisode.state;
            proposal.previousAdmission = admitted.state;
            proposal.strictProofCurrent = proposal.captionConfirmed =
                proposal.captionMeasured = false;
            proposal.captionAbsenceConfirmed = true;
            proposal.currentObservation = physical;
            proposal.currentNativeTrusted = true;
            proposal.currentNativeBounds = physical;
            proposal.currentRetentionSafe = proposal.currentExcludedBandsPixelSafe = true;
            proposal.currentOutwardVisible = false;
            proposal.observationSourceSequence = proposal.retentionSourceSequence = 101;
            const auto clean = BuildColdMaskedCaptionPresentation(proposal);
            Assert::IsTrue(clean.available && clean.held && !clean.releasedToBase,
                L"A single current clean measurement must not snap a displayed caption envelope to physical bars.");
            const auto cleanAdmitted = AdmitCropPresentation(
                admitted.state, crop, clean.presentation, 0);
            Assert::IsFalse(cleanAdmitted.blocked);
            Assert::AreEqual(2044, cleanAdmitted.presentation.sourceBounds.bottom);
            auto cleanCommit = commit;
            cleanCommit.currentEpisode = clean.episodeState;
            cleanCommit.currentAdmission = cleanAdmitted.state;
            cleanCommit.currentSourceSequence = cleanCommit.selectedSourceSequence = 101;
            cleanCommit.proofSourceSequence = 100;
            cleanCommit.selectedEnvelope = clean.presentation.sourceBounds;
            const auto afterClean = CommitColdMaskedCaptionPresentation(cleanCommit);
            Assert::IsTrue(afterClean.committed);
            Assert::AreEqual(1u, afterClean.state.captionCleanSamples);
            Assert::AreEqual(2044, afterClean.state.boundedPresentation.bottom);

            // The next source has the caption again, after a single clean
            // measurement. Its current proof must reset the clean vote and
            // preserve the same bounded output without an intervening snap.
            episode.previous = afterClean.state;
            episode.sourceSequence = 102;
            const auto returnEpisode = EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                int(returnEpisode.state.mode));
            crop.frameSourceSequence = crop.currentVisibleSourceSequence = 102;
            crop.currentVisibleBounds.bottom = 2042;
            proposal.episode = returnEpisode.state;
            proposal.previousAdmission = cleanAdmitted.state;
            proposal.strictProofCurrent = proposal.captionConfirmed = true;
            proposal.captionAbsenceConfirmed = false;
            proposal.currentNativeTrusted = false;
            proposal.currentObservation.bottom = 2010;
            proposal.currentRetentionSafe = proposal.currentExcludedBandsPixelSafe = false;
            proposal.proofSourceSequence = proposal.captionSourceSequence =
                proposal.observationSourceSequence = proposal.retentionSourceSequence = 102;
            const auto returned = BuildColdMaskedCaptionPresentation(proposal);
            Assert::IsTrue(returned.available && !returned.releasedToBase,
                L"A caption returning after one clean source must retain a bounded presentation.");
            Assert::AreEqual(2044, returned.presentation.sourceBounds.bottom);
            Assert::AreEqual(0u, returned.episodeState.captionCleanSamples);
            const auto returnedAdmitted = AdmitCropPresentation(
                cleanAdmitted.state, crop, returned.presentation, 0);
            Assert::IsFalse(returnedAdmitted.blocked);
            Assert::AreEqual(2044, returnedAdmitted.presentation.sourceBounds.bottom);
            auto returnCommit = commit;
            returnCommit.currentEpisode = returned.episodeState;
            returnCommit.currentAdmission = returnedAdmitted.state;
            returnCommit.currentSourceSequence = returnCommit.selectedSourceSequence =
                returnCommit.proofSourceSequence = 102;
            returnCommit.selectedEnvelope = returned.presentation.sourceBounds;
            const auto returnedPresented = CommitColdMaskedCaptionPresentation(returnCommit);
            Assert::IsTrue(returnedPresented.committed);
            Assert::AreEqual(0u, returnedPresented.state.captionCleanSamples);

            // Replay actual source policy, admission, and selected-submit order.
            // At 23.976 Hz the existing 250 ms source-rate rule needs seven
            // consecutive clean sources. A missing worker result interrupts it.
            auto state = returnedPresented.state;
            auto admission = returnedAdmitted.state;
            for (uint64_t sequence = 103; sequence <= 111; ++sequence)
            {
                episode.previous = state;
                episode.sourceSequence = sequence;
                const auto signal = SelectNearBlackEpisodeDarkness(true, true, false);
                episode.nearBlackEvaluated = signal.evaluated;
                episode.globalNearBlack = signal.nearBlack;
                const auto sourceEpisode = EvaluateNearBlackPresentationEpisode(episode);
                Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                    int(sourceEpisode.state.mode));
                crop.frameSourceSequence = crop.currentVisibleSourceSequence = sequence;
                crop.nearBlackEpisodeFullRaster =
                    sourceEpisode.state.mode == NearBlackPresentationMode::FULL_RASTER;
                crop.nearBlackEpisodeRetainCrop =
                    sourceEpisode.state.mode == NearBlackPresentationMode::RETAIN_CROP;
                crop.currentVisibleBounds = physical;
                proposal.episode = sourceEpisode.state;
                proposal.previousAdmission = admission;
                proposal.strictProofCurrent = proposal.captionConfirmed = false;
                proposal.captionMeasured = false;
                proposal.captionAbsenceConfirmed = sequence != 104;
                proposal.currentNativeTrusted = sequence != 104;
                proposal.currentNativeBounds = physical;
                proposal.currentObservation = physical;
                proposal.currentRetentionSafe = proposal.currentExcludedBandsPixelSafe = true;
                proposal.currentOutwardVisible = false;
                proposal.observationSourceSequence = proposal.retentionSourceSequence = sequence;
                proposal.framesPerSecond = 23.976;
                const auto source = BuildColdMaskedCaptionPresentation(proposal);
                Assert::IsTrue(source.available,
                    L"Each current source must retain a safe selected presentation.");
                const bool release = sequence == 111;
                Assert::AreEqual(release, source.releasedToBase,
                    L"Release requires seven consecutive clean source votes after a worker gap.");
                if (!release)
                    Assert::AreEqual(2044, source.presentation.sourceBounds.bottom);
                else
                    Assert::AreEqual(1892, source.presentation.sourceBounds.bottom);
                const auto sourceAdmission = AdmitCropPresentation(
                    admission, crop, source.presentation, 0);
                Assert::IsFalse(sourceAdmission.blocked);
                auto selectedCommit = commit;
                selectedCommit.currentEpisode = source.episodeState;
                selectedCommit.currentAdmission = sourceAdmission.state;
                selectedCommit.action = release
                    ? ColdMaskedCaptionCommitAction::CLEAN_BASE
                    : ColdMaskedCaptionCommitAction::BOUNDED;
                selectedCommit.currentSourceSequence = selectedCommit.selectedSourceSequence = sequence;
                selectedCommit.proofSourceSequence = 102;
                selectedCommit.selectedEnvelope = source.presentation.sourceBounds;
                selectedCommit.selectedActualOwner = sourceAdmission.presentation.owner;
                selectedCommit.selectedActualBounds = sourceAdmission.presentation.sourceBounds;
                selectedCommit.framesPerSecond = 23.976;
                const auto submitted = CommitColdMaskedCaptionPresentation(selectedCommit);
                Assert::IsTrue(submitted.committed,
                    L"Only the selected submitted source advances the cold presentation owner.");
                if (sequence == 103)
                {
                    Assert::AreEqual(1u, submitted.state.captionCleanSamples);
                    auto duplicate = selectedCommit;
                    duplicate.currentEpisode = submitted.state;
                    Assert::IsFalse(CommitColdMaskedCaptionPresentation(duplicate).committed,
                        L"Repeated output of one selected source cannot renew the clean dwell.");
                    proposal.episode = submitted.state;
                    proposal.previousAdmission = sourceAdmission.state;
                    proposal.cadenceRepeat = true;
                    const auto repeatedPolicy = BuildColdMaskedCaptionPresentation(proposal);
                    Assert::IsTrue(repeatedPolicy.available && repeatedPolicy.held);
                    Assert::AreEqual(1u, repeatedPolicy.episodeState.captionCleanSamples,
                        L"Output cadence cannot add a source-rate clean vote.");
                    proposal.cadenceRepeat = false;
                }
                if (sequence == 104)
                    Assert::AreEqual(0u, submitted.state.captionCleanSamples,
                        L"A worker-unknown source interrupts consecutive clean votes.");
                if (sequence >= 105 && sequence < 111)
                    Assert::AreEqual(unsigned(sequence - 104),
                        submitted.state.captionCleanSamples);
                if (release)
                    Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
                        int(submitted.state.mode));
                state = submitted.state;
                admission = sourceAdmission.state;
            }
        }
        TEST_METHOD(CompletedNativeScopeProofAcquiresRetainEpisodeWithoutPriorScopeAdmission)
        {
            const ActivePictureBounds scope{0,272,3840,1884,3840,2160,3840.0/1612,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            Input crop;crop.automaticCropEnabled=true;crop.sharedGeometryAvailable=true;
            crop.geometry=scope;crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.frameSourceGeneration=crop.geometrySourceGeneration=3;
            crop.frameSourceSequence=5253;crop.framePresentationEpoch=5;
            crop.rasterWidth=3840;crop.rasterHeight=2160;crop.nearBlackEpisodeRetainCrop=true;
            ColdMaskedCaptionPresentationInput c;c.crop=&crop;c.framesPerSecond=23.976;
            c.episode.mode=NearBlackPresentationMode::RETAIN_CROP;
            c.episode.sourceGeneration=3;c.episode.presentationEpoch=5;c.episode.startedSourceSequence=5246;
            c.episode.entryTrustedCropAvailable=true;c.episode.entryTrustedCrop=scope;
            c.episode.entryTrustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;
            c.episode.revalidationNativeDarkRun=true;c.episode.revalidationSamples=7;
            c.episode.revalidationLastSourceSequence=5253;
            c.currentObservationAvailable=c.currentRetentionEvaluated=c.currentExcludedBandsPixelSafe=true;
            c.currentObservation=c.retentionBase=scope;
            c.observationSourceGeneration=c.retentionSourceGeneration=3;
            c.observationSourceSequence=c.retentionSourceSequence=5253;
            auto proposal=BuildColdMaskedCaptionPresentation(c);
            Assert::IsTrue(proposal.available&&proposal.nativeCleanBase&&proposal.releasedToBase,
                L"A completed native proof must reach admission even if the episode retained a not-yet-presented crop.");
            crop.latestObservationSupportsCrop=proposal.nativeCleanBase;
            const auto admitted=AdmitCropPresentation({},crop,proposal.presentation,5);
            Assert::IsFalse(admitted.blocked);
            ColdMaskedCaptionCommitInput commit;commit.currentEpisode=proposal.episodeState;
            commit.currentAdmission=admitted.state;commit.action=ColdMaskedCaptionCommitAction::CLEAN_BASE;
            commit.proposalMode=c.episode.mode;commit.proposalEntryCropAvailable=true;
            commit.proposalEntryCrop=scope;commit.proposalStartSequence=5246;
            commit.sourceGeneration=3;commit.presentationEpoch=5;
            commit.currentSourceSequence=commit.selectedSourceSequence=5253;
            commit.selectedBase=commit.selectedActualBounds=scope;commit.selectedActualOwner=DecisionOwner::TRUSTED_CROP;
            commit.framesPerSecond=23.976;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(commit).committed);
            commit.renderedAndSubmitted=true;
            Assert::IsTrue(CommitColdMaskedCaptionPresentation(commit).committed);
            c.episode.revalidationSamples=6;Assert::IsFalse(BuildColdMaskedCaptionPresentation(c).nativeCleanBase);
            c.episode.revalidationSamples=7;c.currentOutwardVisible=true;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(c).nativeCleanBase,L"Real outward content still blocks inward acquisition.");
            c.currentOutwardVisible=false;c.retentionSourceSequence--;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(c).nativeCleanBase);
            c.retentionSourceSequence++;c.episode.sourceGeneration++;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(c).nativeCleanBase);
        }
        TEST_METHOD(SelectedCleanScopeOptionalFillCommitsOnlyItsOwnSourceWitness)
        {
            const ActivePictureBounds scope{0,272,3840,1884,3840,2160,3840.0/1612,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            ColdMaskedCaptionCommitInput c;c.sourceGeneration=3;c.presentationEpoch=5;
            c.currentSourceSequence=5320;c.selectedSourceSequence=5312;c.framesPerSecond=23.976;
            c.renderedAndSubmitted=true;c.action=ColdMaskedCaptionCommitAction::CLEAN_BASE;
            c.proposalMode=NearBlackPresentationMode::FULL_RASTER;c.proposalStartSequence=5272;
            c.currentEpisode.mode=c.proposalMode;c.currentEpisode.sourceGeneration=3;
            c.currentEpisode.presentationEpoch=5;c.currentEpisode.fullRasterStartedSourceSequence=5272;
            c.currentEpisode.entryTrustedCropAvailable=true;c.currentEpisode.entryTrustedCrop=scope;
            c.currentEpisode.entryTrustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;
            c.currentEpisode.revalidationNativeDarkRun=true;c.currentEpisode.revalidationSamples=7;
            c.currentEpisode.revalidationLastSourceSequence=5312;
            c.currentAdmission.available=true;c.currentAdmission.sourceGeneration=3;
            c.currentAdmission.presentationEpoch=5;c.currentAdmission.trustedCrop=scope;
            c.currentAdmission.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            c.selectedBase=c.selectedActualBounds=scope;c.selectedActualOwner=DecisionOwner::TRUSTED_CROP;
            c.selectedActualBounds.left=26;c.selectedActualBounds.right=3814;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(c).committed,L"An unexplained clipped crop cannot commit.");
            c.selectedAdmission=c.currentAdmission;c.selectedAdmission.optionalFillApplied=true;
            c.selectedAdmission.optionalFillSourceBounds=c.selectedActualBounds;
            c.selectedAdmission.optionalFillSourceSequence=5312;
            const auto accepted=CommitColdMaskedCaptionPresentation(c);
            Assert::IsTrue(accepted.committed,L"Actual admitted side fill must not strand the full-raster episode after displaying Scope.");
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(accepted.state.mode));
            Assert::AreEqual(0,accepted.state.entryTrustedCrop.left,L"Logical native picture remains full width.");
            auto bad=c;bad.selectedAdmission.optionalFillSourceSequence=5320;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(bad).committed,L"Newest live output cannot authorize an older selected frame.");
            bad=c;bad.selectedAdmission.presentationEpoch++;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(bad).committed);
            bad=c;bad.selectedActualBounds.top+=2;bad.selectedAdmission.optionalFillSourceBounds=bad.selectedActualBounds;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(bad).committed,L"Side-fill permission cannot clip the top or bottom picture boundary.");
            bad=c;bad.selectedActualOwner=DecisionOwner::VERTICAL_TRANSLATION;
            Assert::IsFalse(CommitColdMaskedCaptionPresentation(bad).committed);
            bad=c;bad.renderedAndSubmitted=false;Assert::IsFalse(CommitColdMaskedCaptionPresentation(bad).committed);
        }
        TEST_METHOD(RawDarkCleanNativeScopeReleasesFullRasterAfterDistinctSafeSources)
        {
            const ActivePictureBounds scope{ 0, 276, 3840, 1884, 3840, 2160,
                3840.0 / 1608.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
            NearBlackPresentationEpisodeInput input;
            input.previous.mode = NearBlackPresentationMode::FULL_RASTER;
            input.previous.sourceGeneration = 1;
            input.previous.presentationEpoch = 5;
            input.previous.startedAtFullRaster = true;
            input.previous.startedSourceSequence = 1016;
            input.previous.fullRasterStartedSourceSequence = 1016;
            input.previous.entryTrustedCropAvailable = true;
            input.previous.entryTrustedCrop = scope;
            input.previous.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
            input.measurementCurrent = true;
            input.trustedCropAvailable = true;
            input.trustedCrop = scope;
            input.trustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
            input.currentObservationAvailable = input.currentNativeObservationAvailable = true;
            input.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
            input.currentObservationOrigin = ActivePictureAuthorityOrigin::NATIVE;
            input.currentObservation = scope;
            input.currentNativeObservation = { 0, 0, 3840, 2160, 3840, 2160,
                3840.0 / 2160.0, ActivePictureBounds::BarAxes::NONE };
            input.currentNativeProposedAvailable = true;
            input.currentNativeProposedBounds = scope;
            input.retentionEvaluated = input.retentionSafe =
                input.retentionExcludedBandsPixelSafe = true;
            input.retentionBounds = scope;
            input.knownTrustedGeometryReacquired = true;
            input.reacquiredTrustedGeometry = scope;
            input.reacquiredTrustedClassification =
                ActivePictureClassification::BAR_CROP_TRUSTED;
            input.reacquisitionIsCurrentAssociation = true;
            input.reacquiredSourceGeneration = input.retentionSourceGeneration =
                input.sourceGeneration = 1;
            input.reacquiredPresentationEpoch = input.presentationEpoch = 5;
            input.framesPerSecond = 23.976;
            CropPresentationAdmissionState admission;
            admission.available = true;
            admission.trustedCrop = scope;
            admission.sourceGeneration = 1;
            admission.presentationEpoch = 5;
            const auto original = input;
            const auto originalAdmission = admission;
            for (uint64_t sequence = 2000; sequence < 2007; ++sequence)
            {
                input.sourceSequence = input.retentionSourceSequence =
                    input.reacquiredSourceSequence = sequence;
                const auto raw = SelectNearBlackEpisodeDarkness(true, true, true);
                input.nearBlackEvaluated = raw.evaluated;
                input.globalNearBlack = raw.nearBlack;
                const auto episode = EvaluateNearBlackPresentationEpisode(input);
                Input crop;
                crop.automaticCropEnabled = true;
                crop.sharedGeometryAvailable = crop.latestObservationSupportsCrop = true;
                crop.classification = crop.latestObservationClassification =
                    ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.geometry = scope;
                crop.geometrySourceGeneration = crop.frameSourceGeneration = 1;
                crop.frameSourceSequence = sequence;
                crop.framePresentationEpoch = 5;
                crop.rasterWidth = 3840;
                crop.rasterHeight = 2160;
                crop.nearBlackEpisodeFullRaster =
                    episode.state.mode == NearBlackPresentationMode::FULL_RASTER;
                ColdMaskedCaptionPresentationInput cold;
                cold.crop = &crop;
                cold.episode = episode.state;
                cold.previousAdmission = admission;
                cold.currentObservationAvailable = cold.currentRetentionEvaluated = true;
                cold.currentObservation = scope;
                cold.observationSourceGeneration = cold.retentionSourceGeneration = 1;
                cold.observationSourceSequence = cold.retentionSourceSequence = sequence;
                cold.retentionBase = scope;
                cold.currentNativeTrusted = cold.currentRetentionSafe =
                    cold.currentExcludedBandsPixelSafe = true;
                cold.captionAbsenceConfirmed = false; // Worker result is still unknown.
                cold.currentNativeBounds = scope;
                cold.framesPerSecond = 23.976;
                const auto proposal = BuildColdMaskedCaptionPresentation(cold);
                const auto candidate = proposal.available ? proposal.presentation : Evaluate(crop);
                const auto admitted = AdmitCropPresentation(admission, crop, candidate, 5);
                Assert::IsFalse(admitted.blocked);
                if (sequence < 2006)
                {
                    Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                        int(episode.state.mode));
                    Assert::IsFalse(admitted.presentation.applyCrop,
                        L"Dark clean bars need a finite source-rate proof before presentation changes.");
                }
                else
                {
                    Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                        int(episode.state.mode),
                        L"Source-only proof cannot change the displayed owner before selected submit.");
                    Assert::IsTrue(proposal.available && proposal.nativeCleanBase &&
                        proposal.releasedToBase,
                        L"Seven distinct current dark-native clean sources must nominate the saved physical Scope despite a missing subtitle worker.");
                    Assert::IsTrue(admitted.presentation.applyCrop);
                    Assert::AreEqual(int(DecisionOwner::TRUSTED_CROP),
                        int(admitted.presentation.owner));
                    Assert::AreEqual(276, admitted.presentation.sourceBounds.top);
                    Assert::AreEqual(1884, admitted.presentation.sourceBounds.bottom);
                    ColdMaskedCaptionCommitInput selected;
                    selected.currentEpisode = proposal.episodeState;
                    selected.currentAdmission = admitted.state;
                    selected.action = ColdMaskedCaptionCommitAction::CLEAN_BASE;
                    selected.proposalMode = episode.state.mode;
                    selected.proposalStartSequence =
                        episode.state.fullRasterStartedSourceSequence;
                    selected.sourceGeneration = 1;
                    selected.presentationEpoch = 5;
                    selected.currentSourceSequence = selected.selectedSourceSequence = sequence;
                    selected.selectedBase = scope;
                    selected.selectedActualOwner = admitted.presentation.owner;
                    selected.selectedActualBounds = admitted.presentation.sourceBounds;
                    selected.framesPerSecond = 23.976;
                    Assert::IsFalse(CommitColdMaskedCaptionPresentation(selected).committed,
                        L"A source-only candidate cannot release the displayed title episode.");
                    selected.renderedAndSubmitted = true;
                    auto wrongOwner = selected;
                    wrongOwner.selectedActualOwner = DecisionOwner::VERTICAL_TRANSLATION;
                    Assert::IsFalse(CommitColdMaskedCaptionPresentation(wrongOwner).committed,
                        L"A late translated output cannot commit a native physical-base release.");
                    auto wrongBounds = selected;
                    wrongBounds.selectedActualBounds.top += 2;
                    Assert::IsFalse(CommitColdMaskedCaptionPresentation(wrongBounds).committed,
                        L"Only the exact selected physical crop may retire the full-raster owner.");
                    auto captureRateMistake = selected;
                    captureRateMistake.framesPerSecond = 60.0;
                    Assert::IsFalse(CommitColdMaskedCaptionPresentation(captureRateMistake).committed,
                        L"The selected commit must use the 23.976 source-rate proof threshold, not a 60 Hz output cadence.");
                    const auto committed = CommitColdMaskedCaptionPresentation(selected);
                    Assert::IsTrue(committed.committed,
                        L"Only selected successful physical Scope output retires the dark full-raster owner.");
                    Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
                        int(committed.state.mode));
                    input.previous = committed.state;
                    admission = admitted.state;
                    continue;
                }
                input.previous = episode.state;
                admission = admitted.state;
            }
            input.sourceSequence = input.retentionSourceSequence =
                input.reacquiredSourceSequence = 2007;
            const auto nextDark = SelectNearBlackEpisodeDarkness(true, true, true);
            input.nearBlackEvaluated = nextDark.evaluated;
            input.globalNearBlack = nextDark.nearBlack;
            const auto retained = EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),
                int(retained.state.mode),
                L"The next raw-dark clean source must keep the committed physical Scope crop.");

            // Each invalid current source breaks a prospective seven-source run.
            // Neither a pending worker nor an old contract supplies pixel proof.
            for (int invalid = 0; invalid < 6; ++invalid)
            {
                auto candidateInput = original;
                auto candidateAdmission = originalAdmission;
                for (uint64_t sequence = 3000; sequence < 3007; ++sequence)
                {
                    candidateInput.sourceSequence =
                        candidateInput.retentionSourceSequence =
                        candidateInput.reacquiredSourceSequence = sequence;
                    candidateInput.cadenceRepeat = false;
                    candidateInput.retentionExcludedBandsPixelSafe = true;
                    candidateInput.boundedVisibleContentOutsideCrop = false;
                    candidateInput.currentNativeObservationAvailable =
                        candidateInput.currentNativeProposedAvailable = true;
                    candidateInput.currentNativeProposedBounds = scope;
                    candidateInput.currentObservationOrigin =
                        ActivePictureAuthorityOrigin::NATIVE;
                    candidateInput.currentNativeObservation = scope;
                    candidateInput.currentObservation = scope;
                    candidateInput.presentationEpoch = 5;
                    candidateInput.reacquiredPresentationEpoch = 5;
                    if (sequence == 3003)
                    {
                        if (invalid == 0)
                        {
                            candidateInput.sourceSequence =
                                candidateInput.retentionSourceSequence =
                                candidateInput.reacquiredSourceSequence = 3002;
                            candidateInput.cadenceRepeat = true;
                        }
                        if (invalid == 1)
                            candidateInput.retentionExcludedBandsPixelSafe = false;
                        if (invalid == 2)
                            candidateInput.boundedVisibleContentOutsideCrop = true;
                        if (invalid == 3)
                            candidateInput.currentNativeProposedAvailable = false;
                        if (invalid == 4)
                            candidateInput.currentNativeProposedBounds.top = 0;
                        if (invalid == 5)
                        {
                            candidateInput.presentationEpoch = 6;
                            candidateInput.reacquiredPresentationEpoch = 6;
                        }
                    }
                    const auto decided = EvaluateNearBlackPresentationEpisode(candidateInput);
                    if (sequence == 3003 && invalid != 0)
                        Assert::AreEqual(0u, decided.state.revalidationSamples,
                            (L"Unsafe current native source must reset episode proof: variant " +
                                std::to_wstring(invalid)).c_str());
                    Input candidateCrop;
                    candidateCrop.automaticCropEnabled = true;
                    candidateCrop.sharedGeometryAvailable =
                        candidateCrop.latestObservationSupportsCrop = true;
                    candidateCrop.classification =
                        candidateCrop.latestObservationClassification =
                            ActivePictureClassification::BAR_CROP_TRUSTED;
                    candidateCrop.geometry = scope;
                    candidateCrop.geometrySourceGeneration =
                        candidateCrop.frameSourceGeneration = 1;
                    candidateCrop.frameSourceSequence = candidateInput.sourceSequence;
                    candidateCrop.framePresentationEpoch = candidateInput.presentationEpoch;
                    candidateCrop.rasterWidth = 3840;
                    candidateCrop.rasterHeight = 2160;
                    candidateCrop.nearBlackEpisodeFullRaster =
                        decided.state.mode == NearBlackPresentationMode::FULL_RASTER;
                    ColdMaskedCaptionPresentationInput cold;
                    cold.crop = &candidateCrop;
                    cold.episode = decided.state;
                    cold.previousAdmission = candidateAdmission;
                    cold.currentObservationAvailable =
                        cold.currentRetentionEvaluated = true;
                    cold.currentObservation = scope;
                    cold.observationSourceGeneration =
                        cold.retentionSourceGeneration = 1;
                    cold.observationSourceSequence =
                        cold.retentionSourceSequence = candidateInput.sourceSequence;
                    cold.retentionBase = scope;
                    cold.currentNativeTrusted =
                        candidateInput.currentNativeObservationAvailable;
                    cold.currentRetentionSafe = true;
                    cold.currentExcludedBandsPixelSafe =
                        candidateInput.retentionExcludedBandsPixelSafe;
                    cold.currentOutwardVisible =
                        candidateInput.boundedVisibleContentOutsideCrop;
                    cold.currentNativeBounds =
                        candidateInput.currentNativeObservation;
                    const auto proposal = BuildColdMaskedCaptionPresentation(cold);
                    Assert::IsFalse(proposal.nativeCleanBase,
                        (L"An invalid source must not produce a dark native base candidate: variant " +
                            std::to_wstring(invalid)).c_str());
                    candidateInput.previous = decided.state;
                }
            }
        }
        TEST_METHOD(TranslatedColdCaptionWorkerGapKeepsPresentedPose)
        {
            Input crop;
            auto request = MakeCaptionFitInput(crop, 3840, 2160,
                268, 1892, 1246, 1778, 2606, 2042);
            const uint64_t source = crop.frameSourceSequence;
            NearBlackPresentationEpisodeInput episode;
            episode.sourceGeneration = crop.frameSourceGeneration;
            episode.presentationEpoch = 3;
            episode.sourceSequence = source;
            episode.framesPerSecond = 23.976;
            episode.measurementCurrent = true;
            const auto darkness = SelectNearBlackEpisodeDarkness(true, true, false);
            episode.nearBlackEvaluated = darkness.evaluated;
            episode.globalNearBlack = darkness.nearBlack;
            const auto firstEpisode = EvaluateNearBlackPresentationEpisode(episode);
            Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
                int(firstEpisode.state.mode));
            crop.framePresentationEpoch = 3;
            crop.nearBlackEpisodeFullRaster = true;
            crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            ColdMaskedCaptionPresentationInput proposal;
            proposal.crop = &crop;
            proposal.episode = firstEpisode.state;
            proposal.strictProofCurrent = proposal.captionConfirmed = true;
            proposal.strictPicture = crop.geometry;
            proposal.proofSourceGeneration = proposal.captionSourceGeneration = crop.frameSourceGeneration;
            proposal.proofSourceSequence = proposal.captionSourceSequence = source;
            proposal.proofPresentationEpoch = 3;
            proposal.captionBounds = request.captionBounds;
            proposal.currentObservationAvailable = proposal.currentRetentionEvaluated = true;
            proposal.currentObservation = crop.currentVisibleBounds;
            proposal.observationSourceGeneration = proposal.retentionSourceGeneration = crop.frameSourceGeneration;
            proposal.observationSourceSequence = proposal.retentionSourceSequence = source;
            proposal.retentionBase = crop.geometry;
            const auto bounded = BuildColdMaskedCaptionPresentation(proposal);
            Assert::IsTrue(bounded.available);
            const auto admitted = AdmitCropPresentation({}, crop, bounded.presentation, 3);
            Assert::IsFalse(admitted.blocked);
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT), int(admitted.presentation.owner));
            EstablishedSubtitleTranslationInput owner;
            owner.base = crop.geometry;
            owner.sourceGeneration = crop.frameSourceGeneration;
            owner.presentation.action = VerticalBarPresentationAction::TRANSLATE;
            owner.presentation.sourceSequence = source - 1;
            owner.presentation.lastDetectionTick = 990;
            owner.holdMs = 5000;
            owner.presentation.translationPixels = 174.0f;
            owner.resolvedTranslationPixels = 34;
            owner.engagingTranslation = owner.engageIntervalAvailable = true;
            owner.engageTargetTranslationPixels = 174.0f;
            owner.engageAppliedTranslationPixels = 34.0f;
            auto translatedInput = crop;
            translatedInput.nearBlackEpisodeFullRaster = false;
            translatedInput.verticalTranslationActive = true;
            translatedInput.verticalTranslationBase = crop.geometry;
            translatedInput.verticalTranslationSourceGeneration = crop.frameSourceGeneration;
            translatedInput.verticalTranslationPixels = 34;
            request.crop = &translatedInput;
            const auto translated = CompleteCaptionAcquisitionTranslation(
                request, admitted, owner, 3, 1000);
            Assert::AreEqual(int(DecisionOwner::VERTICAL_TRANSLATION),
                int(translated.presentation.owner));
            Assert::AreEqual(302, translated.presentation.sourceBounds.top);
            Assert::AreEqual(1926, translated.presentation.sourceBounds.bottom);
            ColdMaskedCaptionCommitInput commit;
            commit.currentEpisode = firstEpisode.state;
            commit.currentAdmission = translated.state;
            commit.action = ColdMaskedCaptionCommitAction::TRANSLATED;
            commit.proposalMode = firstEpisode.state.mode;
            commit.proposalStartSequence = firstEpisode.state.fullRasterStartedSourceSequence;
            commit.sourceGeneration = crop.frameSourceGeneration;
            commit.presentationEpoch = 3;
            commit.currentSourceSequence = commit.selectedSourceSequence =
                commit.proofSourceSequence = source;
            commit.selectedBase = crop.geometry;
            commit.selectedEnvelope = bounded.presentation.sourceBounds;
            commit.selectedTranslatedBounds = translated.presentation.sourceBounds;
            commit.selectedTranslationPixels = translated.presentation.verticalTranslationPixels;
            commit.renderedAndSubmitted = true;
            const auto presented = CommitColdMaskedCaptionPresentation(commit);
            Assert::IsTrue(presented.committed);
            episode.previous = presented.state;
            episode.sourceSequence = source + 1;
            episode.currentTick = 1001;
            episode.trustedCropAvailable = true;
            episode.trustedCrop = crop.geometry;
            episode.currentObservationAvailable =
                episode.currentNativeObservationAvailable =
                episode.retentionEvaluated = episode.retentionSafe =
                episode.retentionExcludedBandsPixelSafe = true;
            episode.currentObservationClassification =
                ActivePictureClassification::BAR_CROP_TRUSTED;
            episode.currentObservation = episode.currentNativeObservation =
                episode.retentionBounds = crop.geometry;
            episode.retentionSourceGeneration = crop.frameSourceGeneration;
            episode.retentionSourceSequence = source + 1;
            const bool retainedTranslation = CanRetainEstablishedScopeForSubtitle(
                episode, translated.state, owner);
            Assert::IsTrue(retainedTranslation,
                L"A committed translation with current native clean-band proof must remain a live pose owner.");
            episode.establishedScopeSubtitleRetained = retainedTranslation;
            const auto nextEpisode = EvaluateNearBlackPresentationEpisode(episode);
            crop.frameSourceSequence = crop.currentVisibleSourceSequence = source + 1;
            crop.nearBlackEpisodeFullRaster =
                nextEpisode.state.mode == NearBlackPresentationMode::FULL_RASTER;
            crop.nearBlackEpisodeRetainCrop =
                nextEpisode.state.mode == NearBlackPresentationMode::RETAIN_CROP;
            crop.currentVisibleBounds = crop.geometry;
            proposal.crop = &crop;
            proposal.episode = nextEpisode.state;
            proposal.previousAdmission = translated.state;
            proposal.strictProofCurrent = proposal.captionConfirmed = false;
            proposal.currentObservation = crop.geometry;
            proposal.currentRetentionSafe = true;
            proposal.observationSourceSequence = proposal.retentionSourceSequence = source + 1;
            const auto gap = BuildColdMaskedCaptionPresentation(proposal);
            Assert::IsTrue(gap.available && gap.held,
                L"A worker gap must retain a current pixel-safe dark-caption presentation.");
            translatedInput.frameSourceSequence =
                translatedInput.currentVisibleSourceSequence = source + 1;
            translatedInput.currentVisibleBounds = crop.geometry;
            translatedInput.nearBlackEpisodeFullRaster = true;
            const auto retained = RetainColdTranslatedPresentation(
                gap, episode, translated.state, owner, translatedInput);
            const auto gapAdmission = AdmitCropPresentation(
                translated.state, translatedInput, retained, 3);
            Assert::IsFalse(gapAdmission.blocked);
            Assert::AreEqual(int(DecisionOwner::VERTICAL_TRANSLATION),
                int(gapAdmission.presentation.owner),
                L"Current native and live-owner proof must preserve the submitted translation route.");
            Assert::AreEqual(translated.presentation.sourceBounds.top,
                gapAdmission.presentation.sourceBounds.top,
                L"A worker gap must not visibly snap the selected translation pose.");
            Assert::AreEqual(translated.presentation.sourceBounds.bottom,
                gapAdmission.presentation.sourceBounds.bottom,
                L"A worker gap must not visibly snap the selected translation pose.");
            auto unsafe = episode;
            unsafe.retentionExcludedBandsPixelSafe = false;
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),
                int(RetainColdTranslatedPresentation(
                    gap, unsafe, translated.state, owner, translatedInput).owner),
                L"Unsafe excluded bands must select the bounded fallback.");
            auto missingAnchor = episode;
            missingAnchor.previous.presentedTranslationAvailable = false;
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),
                int(RetainColdTranslatedPresentation(
                    gap, missingAnchor, translated.state, owner, translatedInput).owner),
                L"An unsubmitted translated pose cannot be invented from held geometry.");
            auto wrongRoute = translatedInput;
            wrongRoute.verticalTranslationPixels += 2;
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),
                int(RetainColdTranslatedPresentation(
                    gap, episode, translated.state, owner, wrongRoute).owner),
                L"A different routed pose needs its own current certificate.");
            auto expired = episode;
            expired.currentTick += owner.holdMs + 1;
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),
                int(RetainColdTranslatedPresentation(
                    gap, expired, translated.state, owner, translatedInput).owner),
                L"The worker gap cannot renew an expired subtitle translation owner.");
        }
        TEST_METHOD(ColdMaskedCaptionProposalRequiresCurrentProofAndKeepsFullCaption)
        {
            Input crop;
            crop.automaticCropEnabled = true;
            crop.nearBlackEpisodeFullRaster = true;
            crop.sharedGeometryAvailable = true;
            crop.latestObservationSupportsCrop = true;
            crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.frameSourceGeneration = crop.geometrySourceGeneration = 7;
            crop.frameSourceSequence = 100;
            crop.framePresentationEpoch = 0; // A cold-start epoch is valid.
            crop.rasterWidth = 3840;
            crop.rasterHeight = 2160;
            crop.geometry = { 0, 268, 3840, 1892, 3840, 2160,
                3840.0 / 1624.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
            crop.acceptedNativeCrop.available = true;
            crop.acceptedNativeCrop.bounds = crop.geometry;
            crop.acceptedNativeCrop.sourceGeneration = 7;
            crop.acceptedNativeCrop.acceptedSourceSequence = 99;
            crop.currentVisibleBoundsAvailable = true;
            crop.currentVisibleBase = crop.geometry;
            crop.currentVisibleBounds = { 0, 268, 3840, 2042, 3840, 2160,
                3840.0 / 1774.0, ActivePictureBounds::BarAxes::NONE };
            crop.currentVisibleSourceGeneration = 7;
            crop.currentVisibleSourceSequence = 100;

            ColdMaskedCaptionPresentationInput input;
            input.crop = &crop;
            input.episode.mode = NearBlackPresentationMode::FULL_RASTER;
            input.episode.sourceGeneration = 7;
            input.episode.presentationEpoch = 0;
            input.strictProofCurrent = true;
            input.strictPicture = crop.geometry;
            input.proofSourceGeneration = 7;
            input.proofSourceSequence = 100;
            input.proofPresentationEpoch = 0;
            input.captionConfirmed = true;
            input.captionBounds = { 1382, 1922, 2454, 2042, 3840, 2160,
                1072.0 / 120.0, ActivePictureBounds::BarAxes::NONE };
            input.captionSourceGeneration = 7;
            input.captionSourceSequence = 100;
            input.currentObservationAvailable = true;
            input.currentObservation = { 0, 276, 3840, 2010, 3840, 2160,
                3840.0 / 1734.0, ActivePictureBounds::BarAxes::NONE };
            input.observationSourceGeneration = 7;
            input.observationSourceSequence = 100;
            input.currentRetentionEvaluated = true;
            input.retentionBase = crop.geometry;
            input.retentionSourceGeneration = 7;
            input.retentionSourceSequence = 100;
            auto proposal = BuildColdMaskedCaptionPresentation(input);
            Assert::IsTrue(proposal.available, L"Current native bars and measured caption must form one bounded proposal.");
            Assert::IsFalse(proposal.held);
            Assert::AreEqual(0, proposal.presentation.sourceBounds.left);
            Assert::AreEqual(268, proposal.presentation.sourceBounds.top);
            Assert::AreEqual(3840, proposal.presentation.sourceBounds.right);
            Assert::AreEqual(2044, proposal.presentation.sourceBounds.bottom);
            Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT), int(proposal.presentation.owner));
            Assert::IsFalse(proposal.presentation.verticallyTranslated,
                L"AR-only caption safety must not enable subtitle translation.");
            const auto ordinaryDisplay = SubtitlePresentationBounds(
                proposal.presentation, SubtitlePreviewMode::None, false, true);
            Assert::AreEqual(268, ordinaryDisplay.top);
            Assert::AreEqual(2044, ordinaryDisplay.bottom);
            auto admitted = AdmitCropPresentation({}, crop, proposal.presentation, 0);
            Assert::IsFalse(admitted.blocked);
            Assert::IsTrue(admitted.presentation.applyCrop && admitted.state.available);
            Assert::AreEqual(2044, admitted.presentation.sourceBounds.bottom);
            Assert::AreEqual(1892, admitted.state.trustedCrop.bottom,
                L"The caption envelope must not replace logical physical picture authority.");

            // Production order: the strict builder supplies current ownership
            // before the first admission. There is no earlier exact-native or
            // FIT submission to break an authorization cycle for this frame.
            Assert::IsTrue(proposal.presentation.confirmedFitOwnership.available);
            Assert::IsTrue(admitted.state.confirmedFitOwnership.available);
            Assert::IsTrue(admitted.state.nativePresentation.available);
            PresentationRecoveryInput late;
            late.crop=crop;late.presentationEpoch=0;late.candidate=admitted.presentation;
            late.previousAdmission=admitted.state;
            late.confirmedFitOwnership=admitted.presentation.confirmedFitOwnership;
            late.confirmedPresentationResolved=true;
            Assert::IsTrue(late.confirmedFitOwnership.available);
            const auto finalCold=ResolveUncertainPicturePresentation(late,admitted.presentation);
            Assert::IsFalse(finalCold.retained);
            Assert::IsTrue(finalCold.presentation.owner==DecisionOwner::OUTWARD_FIT);
            Assert::IsTrue(finalCold.presentation.confirmedFitOwnership.available);
            auto selectedColdAdmission=admitted;
            selectedColdAdmission.presentation.confirmedFitOwnership=finalCold.presentation.confirmedFitOwnership;
            selectedColdAdmission.state.confirmedFitOwnership=finalCold.presentation.confirmedFitOwnership;
            const auto& selectedProof=selectedColdAdmission.state.confirmedFitOwnership;
            Assert::IsTrue(selectedProof.available && selectedProof.origin==ConfirmedFitOwnershipOrigin::CURRENT_CAPTION);
            Assert::AreEqual(uint64_t(7),selectedProof.sourceGeneration);
            Assert::AreEqual(uint64_t(100),selectedProof.sourceSequence);
            Assert::AreEqual(uint64_t(0),selectedProof.presentationEpoch);
            Assert::AreEqual(268,selectedProof.nativeBase.top);
            Assert::AreEqual(1892,selectedProof.nativeBase.bottom);
            Assert::AreEqual(2044,selectedProof.approvedBounds.bottom);
            SubmittedNativeScopeAnchorUpdateInput selectedCommit;
            selectedCommit.frameSubmitted=selectedCommit.nativeCropAvailable=true;
            selectedCommit.nativeCrop=crop.geometry;
            selectedCommit.sourceGeneration=7;selectedCommit.sourceSequence=100;selectedCommit.presentationEpoch=0;
            selectedCommit.selectedOwner=finalCold.presentation.owner;
            selectedCommit.selectedAdmission=selectedColdAdmission.state;
            selectedCommit.submittedBounds=SubtitlePresentationBounds(finalCold.presentation,
                SubtitlePreviewMode::GeneratedGrayBackground,false,false);
            const char* commitReason=nullptr;
            const auto qualifiedSubmission=CommitSubmittedNativeScopeAnchor(selectedCommit,&commitReason);
            Assert::IsTrue(commitReason && std::string(commitReason)=="accepted-native-and-presentation-committed",
                L"The first actual FIT must atomically associate independent native and presentation certificates.");
            Assert::AreEqual(2044,qualifiedSubmission.submittedEnvelope.bottom);
            Assert::AreEqual(uint64_t(100),qualifiedSubmission.submittedEnvelopeSourceSequence);
            Assert::AreEqual(uint64_t(100),qualifiedSubmission.lastSubmittedSourceSequence);
            Assert::AreEqual(1892,qualifiedSubmission.crop.bottom);
            Assert::AreEqual(2044,qualifiedSubmission.submittedPresentation.bottom);
            auto failedFirst=selectedCommit;failedFirst.frameSubmitted=false;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(failedFirst).available);

            auto prior16x9 = admitted.state;
            prior16x9.sourceGeneration = 6;
            prior16x9.trustedCrop = { 0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
                ActivePictureBounds::BarAxes::NONE };
            const auto switched = AdmitCropPresentation(prior16x9, crop, proposal.presentation, 0);
            Assert::IsFalse(switched.blocked);
            Assert::AreEqual(268, switched.state.trustedCrop.top);
            Assert::AreEqual(2044, switched.presentation.sourceBounds.bottom);

            auto bad = input;
            bad.captionSourceSequence = 99;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available);
            bad = input;
            bad.captionSourceGeneration = 6;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available);
            bad = input;
            bad.strictPicture.top += 2;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available);
            bad = input;
            bad.captionBounds.top = 400;bad.captionBounds.bottom = 500;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available,
                L"An inside-only cue cannot authorize outside-picture FIT ownership.");
            bad = input;
            bad.proofPresentationEpoch = 1;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available);
            bad = input;
            bad.currentObservation.bottom = 2080;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available,
                L"A disjoint current pixel outside the proposed envelope must keep full raster.");
            bad = input;
            bad.currentObservationAvailable = false;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available);
            bad = input;
            bad.sceneBoundary = true;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available);
            bad = input;
            bad.crop = nullptr;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(bad).available);

            input.previousAdmission = admitted.state;
            input.episode.boundedPresentationAvailable = true;
            input.episode.boundedPresentation = admitted.presentation.sourceBounds;
            input.episode.boundedPresentationSequence = 100;
            input.episode.boundedPresentationPresentedSequence = 100;
            input.episode.fullRasterStartedSourceSequence = 99;
            input.strictProofCurrent = false;
            input.captionConfirmed = false;
            crop.frameSourceSequence = 101;
            crop.currentVisibleSourceSequence = 101;
            input.observationSourceSequence = 101;
            input.retentionSourceSequence = 101;
            input.currentRetentionSafe = true;
            const auto bridged = BuildColdMaskedCaptionPresentation(input);
            Assert::IsTrue(bridged.available && bridged.held,
                L"A current pixel-safe source may briefly retain only the already admitted envelope.");
            Assert::AreEqual(2044, bridged.presentation.sourceBounds.bottom);
            input.episode.boundedPresentationPresentedSequence = 101;
            for (uint64_t sequence = 102; sequence <= 106; ++sequence)
            {
                crop.frameSourceSequence = sequence;
                crop.currentVisibleSourceSequence = sequence;
                input.observationSourceSequence = sequence;
                input.retentionSourceSequence = sequence;
                const auto continued = BuildColdMaskedCaptionPresentation(input);
                Assert::IsTrue(continued.available && continued.held,
                    L"Every advancing source with fresh safe pixels keeps the already admitted envelope.");
                Assert::AreEqual(2044, continued.presentation.sourceBounds.bottom);
                input.episode.boundedPresentationPresentedSequence = sequence;
            }
            input.currentRetentionSafe = false;
            crop.currentVisibleSourceSequence = 100;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(input).available,
                L"A stale visible witness cannot preserve a dark caption envelope.");
            crop.frameSourceSequence = 106;
            crop.currentVisibleSourceSequence = 106;
            input.observationSourceSequence = 106;
            input.retentionSourceSequence = 106;
            input.cadenceRepeat = true;
            input.currentRetentionSafe = true;
            const auto paused = BuildColdMaskedCaptionPresentation(input);
            Assert::IsTrue(paused.available && paused.held,
                L"An identical paused source keeps its admitted envelope with fresh pixel safety.");
            input.cadenceRepeat = false;
            crop.frameSourceSequence = 107;
            crop.currentVisibleBoundsAvailable = false;
            input.observationSourceSequence = 107;
            input.retentionSourceSequence = 107;
            input.currentObservation = crop.geometry;
            input.currentNativeTrusted = true;
                        input.captionAbsenceConfirmed = true;
input.currentNativeBounds = crop.geometry;
            input.currentExcludedBandsPixelSafe = true;
            input.currentOutwardVisible = false;
            const auto clearCue = BuildColdMaskedCaptionPresentation(input);
            Assert::IsTrue(clearCue.available && clearCue.held && !clearCue.releasedToBase,
                L"One clean native source begins revalidation without flashing away the admitted caption envelope.");
            Assert::AreEqual(1u, clearCue.episodeState.captionCleanSamples);
            Assert::AreEqual(2044, clearCue.presentation.sourceBounds.bottom);
            const auto heldAdmitted = AdmitCropPresentation(input.previousAdmission,
                crop, clearCue.presentation, 0);
            Assert::IsFalse(heldAdmitted.blocked);
            Assert::AreEqual(2044, heldAdmitted.presentation.sourceBounds.bottom);
            auto unsafeClear = input;
            unsafeClear.currentExcludedBandsPixelSafe = false;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(unsafeClear).releasedToBase);
            unsafeClear = input;
            unsafeClear.currentOutwardVisible = true;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(unsafeClear).releasedToBase);
            unsafeClear = input;
            unsafeClear.currentNativeBounds.top = 0;
            Assert::IsFalse(BuildColdMaskedCaptionPresentation(unsafeClear).releasedToBase);
        }
    };

    TEST_CLASS(AutomaticPictureContinuityTests)
    {
        static PresentationRecoveryInput Uncertain(uint64_t sequence = 1832)
        {
            PresentationRecoveryInput input;
            input.crop = TrustedScopeCrop();
            input.crop.geometry = {0, 280, 3840, 1880, 3840, 2160, 2.4,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            input.crop.frameSourceSequence = sequence;
            input.crop.latestObservationSupportsCrop = false;
            input.crop.latestObservationIsProvisional = true;
            input.crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
            input.crop.presentationFailOpen = true;
            input.presentationEpoch = 9;
            input.submittedScopeAnchor.available = true;
            input.submittedScopeAnchor.crop = input.crop.geometry;
            input.submittedScopeAnchor.sourceGeneration = 7;
            input.submittedScopeAnchor.presentationEpoch = 9;
            input.submittedScopeAnchor.lastSubmittedSourceSequence = 1831;
            return input;
        }
        static UncertainPicturePresentationDecision Resolve(const PresentationRecoveryInput& input)
        {
            return ResolveUncertainPicturePresentation(input, Evaluate(input.crop));
        }
        static void Visible(PresentationRecoveryInput& input, int top, int bottom)
        {
            auto& crop = input.crop;
            crop.currentVisibleBoundsAvailable = true;
            crop.currentVisibleBase = input.submittedScopeAnchor.crop;
            crop.currentVisibleSourceGeneration = crop.frameSourceGeneration;
            crop.currentVisibleSourceSequence = crop.frameSourceSequence;
            crop.currentVisibleBounds = crop.currentVisibleBase;
            crop.currentVisibleBounds.top = top;
            crop.currentVisibleBounds.bottom = bottom;
            crop.currentVisibleBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
        }
        static UncertainPicturePresentationDecision QualifiedVisible(PresentationRecoveryInput& input)
        {
            // A real measured extent still needs the existing independently
            // confirmed caption/FIT owner before it can change an established
            // presentation. A bare rectangle is no longer that authority.
            input.measurementCurrent = input.retentionEvaluated = true;
            input.retentionBounds = input.crop.geometry;
            input.retentionSourceGeneration = input.crop.frameSourceGeneration;
            input.retentionSourceSequence = input.crop.frameSourceSequence;
            input.confirmedPresentationResolved = true;
            input.candidate = {};
            input.candidate.applyCrop = true;
            input.candidate.owner = DecisionOwner::OUTWARD_FIT;
            input.candidate.outwardExpanded = true;
            input.candidate.sourceBounds = input.crop.currentVisibleBounds;
            input.candidate.sourceBounds.top &= ~1;
            input.candidate.sourceBounds.bottom =
                (std::min)(input.crop.rasterHeight, (input.candidate.sourceBounds.bottom + 1) & ~1);
            QualifyExplicitCaptionFitFixture(input,input.crop.currentVisibleBounds);
            return ResolveUncertainPicturePresentation(input, input.candidate);
        }
        static SubmittedNativeScopeAnchorUpdateInput CommitInput(const PresentationRecoveryInput& input,
            const UncertainPicturePresentationDecision& resolved)
        {
            SubmittedNativeScopeAnchorUpdateInput commit;
            commit.previous = input.submittedScopeAnchor;
            commit.nativeCropAvailable = true;
            commit.nativeCrop = input.submittedScopeAnchor.crop;
            commit.submittedBounds = resolved.presentation.sourceBounds;
            commit.selectedOwner = resolved.presentation.owner;
            commit.frameSubmitted = true;
            commit.sourceGeneration = input.crop.frameSourceGeneration;
            commit.sourceSequence = input.crop.frameSourceSequence;
            commit.presentationEpoch = input.presentationEpoch;
            return commit;
        }
    public:
        TEST_METHOD(ExpiredRecoveryAndBlockedAdmissionStillUseSubmittedGeometry)
        {
            auto input=Uncertain();
            input.candidate=Evaluate(input.crop);
            input.framesPerSecond=24;
            input.previousAdmission.available=true;
            input.previousAdmission.trustedCrop=input.crop.geometry;
            input.previousAdmission.sourceGeneration=7;
            input.previousAdmission.presentationEpoch=9;
            const auto recovery=EvaluatePresentationRecovery(input);
            const auto admission=AdmitCropPresentation(input.previousAdmission,
                input.crop,recovery.presentation,9);
            const auto final=ResolveUncertainPicturePresentation(input,admission.presentation);
            Assert::IsTrue(final.retained && final.presentation.applyCrop);
            Assert::AreEqual(280,final.presentation.sourceBounds.top);
            Assert::AreEqual(1880,final.presentation.sourceBounds.bottom);
            auto blocked=admission.presentation;
            blocked.reason="unpresented crop excludes current visible pixels";
            Assert::IsTrue(ResolveUncertainPicturePresentation(input,blocked).retained);
        }
        TEST_METHOD(AnUnchangedNativeHoldPreservesConfiguredSideFillAndNlsInput)
        {
            const auto input=Uncertain();
            const auto held=Resolve(input);
            AspectLimitFillInput fill;
            fill.trustedContentAuthorityAccepted=held.presentation.applyCrop;
            fill.cropWiderContentToFillScreen=fill.widerLimitConfigured=true;
            fill.widerAspectLimit=2.41;
            fill.screenAspect=2.35;
            fill.sourceBounds=held.presentation.sourceBounds;
            fill.contentReferenceAvailable=true;
            fill.contentReferenceBounds=held.nativeCrop;
            const auto filled=EvaluateAspectLimitFill(fill);
            Assert::IsTrue(filled.applied);
            Assert::AreEqual(40,filled.sourceBounds.left);
            Assert::AreEqual(3800,filled.sourceBounds.right);
            const auto nls=ResolveNlsPresentationSourceGeometry(true,true,
                held.nativeCrop.left,held.nativeCrop.top,held.nativeCrop.right,held.nativeCrop.bottom,
                held.presentation.applyCrop,held.presentation.sourceBounds.left,held.presentation.sourceBounds.top,
                held.presentation.sourceBounds.right,held.presentation.sourceBounds.bottom,3840,2160);
            Assert::IsTrue(nls.valid);
            Assert::AreEqual(280,nls.top);
            Assert::AreEqual(1880,nls.bottom);
        }
        TEST_METHOD(LateFullRasterCannotClearMoreRecentSubmittedEnvelope)
        {
            auto input=Uncertain();
            Visible(input,280,2040);
            auto commit=CommitInput(input,QualifiedVisible(input));
            commit.previous=CommitSubmittedNativeScopeAnchor(commit);
            commit.sourceSequence=1831;
            commit.fullRasterAuthority=true;
            const auto held=CommitSubmittedNativeScopeAnchor(commit);
            Assert::IsTrue(held.available && held.submittedEnvelopeAvailable);
            Assert::AreEqual(2040,held.submittedEnvelope.bottom);
            input.submittedScopeAnchor=held;
            input.crop.frameSourceSequence=1831;
            Assert::IsFalse(Resolve(input).retained,
                L"A future presentation cannot supply an older frame's geometry.");
        }
        TEST_METHOD(InspectionTimeoutCannotInventFullRaster)
        {
            const auto input = Uncertain();
            Assert::IsFalse(Evaluate(input.crop).applyCrop);
            const auto held = Resolve(input);
            Assert::IsTrue(held.retained && !held.expanded && held.presentation.applyCrop);
            Assert::AreEqual(280, held.presentation.sourceBounds.top);
            Assert::AreEqual(1880, held.presentation.sourceBounds.bottom);
            Assert::IsTrue(held.presentation.owner == DecisionOwner::SUBMITTED_SCOPE_RETENTION);
            Assert::IsFalse(input.crop.latestObservationSupportsCrop);
        }
        TEST_METHOD(DarknessAndMissingAnalysisHaveNoReleaseDeadline)
        {
            auto input = Uncertain();
            input.crop.presentationFailOpen = false;
            input.crop.nearBlackEpisodeFullRaster = true;
            input.crop.latestObservationIsUnavailable = true;
            input.crop.latestObservationIsProvisional = false;
            input.crop.latestObservationClassification = ActivePictureClassification::UNAVAILABLE;
            input.crop.sharedGeometryAvailable = false;
            for (const uint64_t sequence : {1832ull, 2400ull, 1000000ull})
            {
                input.crop.frameSourceSequence = sequence;
                const auto held = Resolve(input);
                Assert::IsTrue(held.retained && held.presentation.applyCrop);
                Assert::AreEqual(280, held.presentation.sourceBounds.top);
                Assert::AreEqual(1880, held.presentation.sourceBounds.bottom);
            }
        }
        TEST_METHOD(StartupCannotAcquireAnUnsubmittedCrop)
        {
            auto input = Uncertain();
            input.submittedScopeAnchor = {};
            const auto unknown = Resolve(input);
            Assert::IsFalse(unknown.retained || unknown.presentation.applyCrop);
        }
        TEST_METHOD(ExplicitFullRasterAndCommittedFullRasterRemainValid)
        {
            auto input = Uncertain();
            input.crop.fullRasterPresentationAuthoritative = true;
            Assert::IsFalse(Resolve(input).retained);
            input.crop.fullRasterPresentationAuthoritative = false;
            input.crop.classification = ActivePictureClassification::FULL_RASTER_TRUSTED;
            input.crop.geometry = {0,0,3840,2160,3840,2160,16.0/9.0,ActivePictureBounds::BarAxes::NONE};
            Assert::IsFalse(Resolve(input).retained);
        }
        TEST_METHOD(ManualFullRasterIsNotOverridden)
        {
            auto input = Uncertain();
            input.crop.automaticCropEnabled = false;
            Assert::IsFalse(Resolve(input).retained);
        }
        TEST_METHOD(SourceViewportRasterAndFutureAnchorAreRejected)
        {
            for (int variant=0; variant<6; ++variant)
            {
                auto input = Uncertain();
                if (variant==0) ++input.crop.frameSourceGeneration;
                if (variant==1) ++input.presentationEpoch;
                if (variant==2) input.crop.rasterWidth = 1920;
                if (variant==3) input.crop.rasterHeight = 1080;
                if (variant==4) input.crop.frameSourceSequence = 1830;
                if (variant==5) input.crop.frameSourceGeneration = 0;
                Assert::IsFalse(Resolve(input).retained);
            }
        }
        TEST_METHOD(NonstandardAndPillarboxPicturesUseActualBounds)
        {
            auto input = Uncertain();
            for (const auto axes : {ActivePictureBounds::BarAxes::TOP_BOTTOM,
                 ActivePictureBounds::BarAxes::LEFT_RIGHT})
            {
                auto bounds = axes == ActivePictureBounds::BarAxes::TOP_BOTTOM
                    ? ActivePictureBounds{0,194,3840,1966,3840,2160,3840.0/1772,axes}
                    : ActivePictureBounds{342,0,3498,2160,3840,2160,3156.0/2160,axes};
                input.submittedScopeAnchor.crop = bounds;
                input.crop.geometry = bounds;
                const auto held = Resolve(input);
                Assert::IsTrue(held.retained);
                Assert::AreEqual(bounds.left, held.presentation.sourceBounds.left);
                Assert::AreEqual(bounds.top, held.presentation.sourceBounds.top);
                Assert::AreEqual(bounds.right, held.presentation.sourceBounds.right);
                Assert::AreEqual(bounds.bottom, held.presentation.sourceBounds.bottom);
            }
        }
        TEST_METHOD(ConfirmedOutwardPixelsExpandOnlyAffectedBoundaries)
        {
            auto input = Uncertain();
            Visible(input,280,2101);
            const auto expanded = QualifiedVisible(input);
            Assert::IsFalse(expanded.retained);
            Assert::IsTrue(expanded.presentation.outwardExpanded);
            Assert::AreEqual(280, expanded.presentation.sourceBounds.top);
            Assert::AreEqual(2102, expanded.presentation.sourceBounds.bottom);
            Assert::IsTrue(expanded.presentation.owner == DecisionOwner::OUTWARD_FIT);
            Assert::IsTrue(expanded.presentation.sourceBounds.trustedBarAxes == ActivePictureBounds::BarAxes::NONE);
        }
        TEST_METHOD(ConfirmedPixelsAtAllEdgesCanRequireFullRaster)
        {
            auto input = Uncertain();
            Visible(input,0,2160);
            const auto full = QualifiedVisible(input);
            Assert::IsFalse(full.retained);
            Assert::IsTrue(full.presentation.outwardExpanded);
            Assert::AreEqual(0, full.presentation.sourceBounds.top);
            Assert::AreEqual(2160, full.presentation.sourceBounds.bottom);
            Assert::IsFalse(input.crop.fullRasterPresentationAuthoritative,
                L"A presentation envelope does not manufacture detector authority.");
        }
        TEST_METHOD(StaleInvalidAndUnrelatedExtentsCannotChangeAR)
        {
            for (int variant=0;variant<5;++variant)
            {
                auto input=Uncertain();
                Visible(input,0,2160);
                if (variant==0) --input.crop.currentVisibleSourceSequence;
                if (variant==1) ++input.crop.currentVisibleSourceGeneration;
                if (variant==2) input.crop.currentVisibleBase.top = 282;
                if (variant==3) input.crop.currentVisibleBounds.rasterWidth = 1920;
                if (variant==4) input.crop.currentVisibleBounds.bottom = 2200;
                const auto held=Resolve(input);
                Assert::IsTrue(held.retained && !held.expanded);
            }
        }
        TEST_METHOD(LoggedDarkPartialObservationIncludesOnlyDisputedEdgeRows)
        {
            auto input = Uncertain();
            input.measurementCurrent = input.retentionEvaluated = input.excludedBandsPixelSafe = true;
            input.observationAvailable = true;
            input.retentionSourceGeneration = 7;
            input.retentionSourceSequence = 1832;
            input.retentionBounds = input.crop.geometry;
            input.observationClassification = ActivePictureClassification::PROVISIONAL;
            for (const auto observation : {
                ActivePictureBounds{276,278,3840,1786,3840,2160,0,ActivePictureBounds::BarAxes::NONE},
                ActivePictureBounds{36,278,3840,1864,3840,2160,0,ActivePictureBounds::BarAxes::NONE}})
            {
                input.observation=observation;
                const auto held=Resolve(input);
                Assert::IsTrue(held.retained && held.expanded);
                Assert::AreEqual(0,held.presentation.sourceBounds.left);
                Assert::AreEqual(278,held.presentation.sourceBounds.top);
                Assert::AreEqual(1880,held.presentation.sourceBounds.bottom);
            }
            input.retentionSourceSequence=1831;
            Assert::IsFalse(Resolve(input).expanded);
            input.retentionSourceSequence=1832;
            input.observation.top=0;
            Assert::IsFalse(Resolve(input).expanded,
                L"A provisional rectangle alone is not proof of a full-raster picture.");
        }
        TEST_METHOD(ResolvedNewCropAndSubtitleTranslationKeepTheirOwners)
        {
            auto input=Uncertain();
            Decision labelOnly;
            labelOnly.applyCrop=true;
            labelOnly.sourceBounds=input.crop.geometry;
            labelOnly.sourceBounds.top=100;
            labelOnly.sourceBounds.bottom=2060;
            labelOnly.sourceBounds.aspectRatio=3840.0/1960.0;
            labelOnly.owner=DecisionOwner::TRUSTED_CROP;
            const auto unsupported=ResolveUncertainPicturePresentation(input,labelOnly);
            Assert::IsTrue(unsupported.retained,
                L"Relabeling an expanded rectangle cannot replace unchanged uncertain native evidence.");
            Assert::AreEqual(280,unsupported.presentation.sourceBounds.top);
            Assert::AreEqual(1880,unsupported.presentation.sourceBounds.bottom);

            // A real current native replacement changes the geometry contract,
            // not merely the proposed decision's owner and display rectangle.
            input.crop.geometry=labelOnly.sourceBounds;
            input.crop.framePresentationEpoch=input.presentationEpoch;
            input.crop.latestObservationSupportsCrop=true;
            input.crop.latestObservationIsProvisional=false;
            input.crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.crop.presentationFailOpen=false;
            input.measurementCurrent=input.observationAvailable=true;
            input.observation=input.crop.geometry;
            input.observationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            input.candidate=Evaluate(input.crop);
            Assert::IsTrue(input.candidate.owner==DecisionOwner::TRUSTED_CROP);
            const auto physical=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(physical.retained);
            Assert::IsTrue(physical.presentation.owner==DecisionOwner::TRUSTED_CROP);
            Assert::AreEqual(100,physical.presentation.sourceBounds.top);
            Assert::AreEqual(2060,physical.presentation.sourceBounds.bottom);

            // Exercise an actual same-height translated decision with the
            // current confirmation contract used by the renderer.
            input=Uncertain();
            input.crop.presentationFailOpen=false;
            input.crop.framePresentationEpoch=input.presentationEpoch;
            input.crop.verticalTranslationActive=true;
            input.crop.verticalTranslationPixels=96;
            input.crop.verticalTranslationBase=input.crop.geometry;
            input.crop.verticalTranslationSourceGeneration=input.crop.frameSourceGeneration;
            input.measurementCurrent=input.retentionEvaluated=input.confirmedPresentationResolved=true;
            input.retentionBounds=input.crop.geometry;
            input.retentionSourceGeneration=input.crop.frameSourceGeneration;
            input.retentionSourceSequence=input.crop.frameSourceSequence;
            input.candidate=Evaluate(input.crop);
            Assert::IsTrue(input.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION &&
                input.candidate.verticallyTranslated);
            const auto translated=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(translated.retained);
            Assert::IsTrue(translated.presentation.owner==DecisionOwner::VERTICAL_TRANSLATION);
            Assert::AreEqual(96,translated.presentation.verticalTranslationPixels);
            Assert::AreEqual(376,translated.presentation.sourceBounds.top);
            Assert::AreEqual(1976,translated.presentation.sourceBounds.bottom);
            Assert::AreEqual(input.crop.geometry.bottom-input.crop.geometry.top,
                translated.presentation.sourceBounds.bottom-translated.presentation.sourceBounds.top);
        }
        TEST_METHOD(RejectedNewCropFallsBackToPriorSubmittedPictureOnly)
        {
            auto input=Uncertain();
            input.crop.geometry.top=400;
            input.crop.geometry.bottom=1760;
            const auto prior=Resolve(input);
            Assert::IsTrue(prior.retained);
            Assert::AreEqual(280,prior.nativeCrop.top);
            Assert::AreEqual(1880,prior.nativeCrop.bottom);
        }
        TEST_METHOD(EnvelopeIsRetainedOnlyAfterSuccessfulSubmission)
        {
            auto input=Uncertain();
            Visible(input,280,2040);
            const auto resolved=QualifiedVisible(input);
            auto commit=CommitInput(input,resolved);
            commit.frameSubmitted=false;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(commit).submittedEnvelopeAvailable);
            commit.frameSubmitted=true;
            input.submittedScopeAnchor=CommitSubmittedNativeScopeAnchor(commit);
            Assert::IsTrue(input.submittedScopeAnchor.submittedEnvelopeAvailable);
            ++input.crop.frameSourceSequence;
            input.crop.currentVisibleBoundsAvailable=false;
            const auto held=Resolve(input);
            Assert::IsTrue(held.expanded && !held.currentPixelExtent);
            Assert::AreEqual(2040,held.presentation.sourceBounds.bottom);
            Assert::AreEqual(1880,held.nativeCrop.bottom);
        }
        TEST_METHOD(DisplayedPendingOverlayEnvelopeSurvivesAnalysisGapWithoutRenewingNativeAr)
        {
            for(auto owner:{DecisionOwner::TRANSLATION_CONFIRMATION,DecisionOwner::FIT_CONFIRMATION}) {
                auto input=Uncertain();Visible(input,280,2040);
                auto commit=CommitInput(input,QualifiedVisible(input));
                commit.selectedOwner=owner;
                commit.selectedAdmission.available=true;
                commit.selectedAdmission.trustedCrop=commit.nativeCrop;
                commit.selectedAdmission.sourceGeneration=commit.sourceGeneration;
                commit.selectedAdmission.presentationEpoch=commit.presentationEpoch;
                const auto nativeSequence=commit.previous.lastSubmittedSourceSequence;
                commit.frameSubmitted=false;
                Assert::IsFalse(CommitSubmittedNativeScopeAnchor(commit).submittedEnvelopeAvailable);
                commit.frameSubmitted=true;
                input.submittedScopeAnchor=CommitSubmittedNativeScopeAnchor(commit);
                Assert::IsTrue(input.submittedScopeAnchor.submittedEnvelopeAvailable);
                Assert::AreEqual(nativeSequence,input.submittedScopeAnchor.lastSubmittedSourceSequence);
                ++input.crop.frameSourceSequence;input.crop.currentVisibleBoundsAvailable=false;
                const auto retained=Resolve(input);
                Assert::IsTrue(retained.expanded);
                Assert::AreEqual(2040,retained.presentation.sourceBounds.bottom);
                Assert::AreEqual(1880,retained.nativeCrop.bottom);
                for(int bad=0;bad<5;++bad) {
                    auto mismatch=commit;
                    if(bad==0)++mismatch.selectedAdmission.sourceGeneration;
                    if(bad==1)++mismatch.selectedAdmission.presentationEpoch;
                    if(bad==2)mismatch.selectedAdmission.available=false;
                    if(bad==3)mismatch.selectedAdmission.trustedCrop.top+=2;
                    if(bad==4)mismatch.previous={};
                    Assert::IsFalse(CommitSubmittedNativeScopeAnchor(mismatch).submittedEnvelopeAvailable);
                }
            }
        }
        TEST_METHOD(ExpandedPresentationCannotManufactureFirstNativeAnchor)
        {
            auto input=Uncertain();
            Visible(input,280,2040);
            auto commit=CommitInput(input,QualifiedVisible(input));
            commit.previous={};
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(commit).available);
        }
        TEST_METHOD(CurrentNativeSubmissionClearsTemporaryEnvelope)
        {
            auto input=Uncertain();
            Visible(input,280,2040);
            auto commit=CommitInput(input,QualifiedVisible(input));
            commit.previous=CommitSubmittedNativeScopeAnchor(commit);
            ++commit.sourceSequence;
            commit.selectedOwner=DecisionOwner::TRUSTED_CROP;
            commit.submittedBounds=commit.nativeCrop;
            const auto native=CommitSubmittedNativeScopeAnchor(commit);
            Assert::IsTrue(native.available);
            Assert::IsFalse(native.submittedEnvelopeAvailable);
            commit.fullRasterAuthority=true;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(commit).available);
        }
        TEST_METHOD(NeitherRejectedSubmissionNorTranslationCanRenewAnEnvelope)
        {
            auto input=Uncertain();
            Visible(input,280,2040);
            auto commit=CommitInput(input,QualifiedVisible(input));
            commit.previous=CommitSubmittedNativeScopeAnchor(commit);
            commit.sourceSequence=1831;
            commit.submittedBounds.bottom=2100;
            Assert::AreEqual(2040,CommitSubmittedNativeScopeAnchor(commit).submittedEnvelope.bottom);
            commit.sourceSequence=1833;
            commit.selectedOwner=DecisionOwner::VERTICAL_TRANSLATION;
            commit.submittedBounds.top=400;
            commit.submittedBounds.bottom=2000;
            const auto retired=CommitSubmittedNativeScopeAnchor(commit);
            Assert::IsFalse(retired.submittedEnvelopeAvailable);
            Assert::AreEqual(commit.previous.lastSubmittedSourceSequence,retired.lastSubmittedSourceSequence,
                L"A presentation handoff cannot renew native picture authority.");
        }
        TEST_METHOD(PresentedFullExtentSurvivesAnAnalysisGapWithoutChangingNativeAR)
        {
            auto input=Uncertain();
            Visible(input,0,2160);
            input.submittedScopeAnchor=CommitSubmittedNativeScopeAnchor(CommitInput(input,QualifiedVisible(input)));
            input.crop.currentVisibleBoundsAvailable=false;
            ++input.crop.frameSourceSequence;
            const auto held=Resolve(input);
            Assert::IsTrue(held.retained && held.expanded);
            Assert::IsFalse(held.presentation.applyCrop);
            Assert::AreEqual(280,held.nativeCrop.top);
        }
    };

    TEST_CLASS(PendingOverlayEnvelopeTests)
    {
    public:
        static PresentationRecoveryInput RewindRecovery(uint64_t sequence)
        {
            auto r = ToyStoryNearBlackRecovery(sequence);
            const ActivePictureBounds base{0,280,3840,1880,3840,2160,2.4,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            r.crop.geometry = r.crop.currentVisibleBase = r.retentionBounds = base;
            r.previousAdmission.trustedCrop = r.submittedScopeAnchor.crop = base;
            r.crop.currentVisibleBounds = base;
            r.crop.currentVisibleBounds.bottom = 1999;
            r.crop.currentVisibleBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
            r.observation = {608,740,3840,1604,3840,2160,3232.0/864.0,
                ActivePictureBounds::BarAxes::NONE};
            r.observationClassification = r.crop.latestObservationClassification =
                ActivePictureClassification::PROVISIONAL;
            r.crop.latestObservationIsProvisional = true;
            r.crop.nearBlackEpisodeFullRaster = false;
            r.crop.nearBlackEpisodeRetainCrop = true;
            r.crop.frameLocalPresentationRetentionEvaluated = true;
            r.globalNearBlack = true;
            r.candidate = Evaluate(r.crop);
            return r;
        }

        static NearBlackPresentationEpisodeInput EpisodeInput(
            const PresentationRecoveryInput& r, const NearBlackPresentationEpisodeState& previous)
        {
            NearBlackPresentationEpisodeInput e;
            e.previous = previous;
            e.sourceGeneration = r.crop.frameSourceGeneration;
            e.sourceSequence = r.crop.frameSourceSequence;
            e.presentationEpoch = r.presentationEpoch;
            e.framesPerSecond = 24;
            e.measurementCurrent = e.nearBlackEvaluated = e.retentionEvaluated = true;
            e.globalNearBlack = r.globalNearBlack;
            e.trustedCropAvailable = e.previouslyAdmittedCropAvailable = true;
            e.trustedCrop = e.previouslyAdmittedCrop = e.retentionBounds = r.crop.geometry;
            e.previouslyAdmittedCropSourceGeneration = e.retentionSourceGeneration = e.sourceGeneration;
            e.previouslyAdmittedCropPresentationEpoch = e.presentationEpoch;
            e.retentionSourceSequence = e.sourceSequence;
            e.currentObservationAvailable = true;
            e.currentObservation = r.observation;
            e.currentObservationClassification = r.observationClassification;
            e.boundedVisibleContentOutsideCrop = true;
            e.currentVisibleBoundsAvailable = true;
            e.currentVisibleBounds = r.crop.currentVisibleBounds;
            e.currentVisibleSourceGeneration = e.sourceGeneration;
            e.currentVisibleSourceSequence = e.sourceSequence;
            e.subtitleInspectionBars = e.trustedCrop;
            e.subtitleInspectionGeneration = e.sourceGeneration;
            return e;
        }

        static Decision ConfirmCaptionFit(PresentationRecoveryInput& r)
        {
            // Fixture origin is a current independently confirmed caption/FIT,
            // never the raw visible rectangle used by uncertain recovery.
            r.measurementCurrent=r.retentionEvaluated=true;
            r.retentionBounds=r.crop.geometry;
            r.retentionSourceGeneration=r.crop.frameSourceGeneration;
            r.retentionSourceSequence=r.crop.frameSourceSequence;
            r.confirmedPresentationResolved=true;
            r.crop.outwardPresentationActive=r.crop.outwardExpansionAvailable=true;
            r.crop.outwardExpansion=r.crop.currentVisibleBounds;
            r.crop.outwardExpansion.top&=~1;
            r.crop.outwardExpansion.bottom=(std::min)(r.crop.rasterHeight,
                (r.crop.outwardExpansion.bottom+1)&~1);
            r.crop.outwardExpansion.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            r.crop.outwardExpansion.aspectRatio=double(r.crop.outwardExpansion.right-r.crop.outwardExpansion.left)/
                (r.crop.outwardExpansion.bottom-r.crop.outwardExpansion.top);
            r.crop.outwardExpansionSourceGeneration=r.crop.frameSourceGeneration;
            auto currentOwner=r.crop;
            currentOwner.nearBlackEpisodeRetainCrop=currentOwner.nearBlackEpisodeFullRaster=false;
            r.candidate=Evaluate(currentOwner);
            Assert::IsTrue(r.candidate.applyCrop && r.candidate.owner==DecisionOwner::OUTWARD_FIT);
            QualifyExplicitCaptionFitFixture(r,r.crop.outwardExpansion);
            return r.candidate;
        }
        static SubmittedNativeScopeAnchor CommitCaptionFit(const PresentationRecoveryInput& r,
            const CropPresentationAdmissionState& admission,const Decision& selected,bool submitted)
        {
            SubmittedNativeScopeAnchorUpdateInput update;
            update.previous=r.submittedScopeAnchor;update.selectedAdmission=admission;
            update.nativeCropAvailable=true;update.nativeCrop=r.crop.geometry;
            update.submittedBounds=selected.sourceBounds;update.selectedOwner=selected.owner;
            update.sourceGeneration=r.crop.frameSourceGeneration;
            update.sourceSequence=r.crop.frameSourceSequence;update.presentationEpoch=r.presentationEpoch;
            update.frameSubmitted=submitted;
            return CommitSubmittedNativeScopeAnchor(update);
        }
        TEST_METHOD(RewindCandidateCannotToggleSubmittedEnvelopeAtDarkThreshold)
        {
            // The known caption FIT is explicitly confirmed and submitted once.
            // Later unknown frames retain that owned envelope as p90 crosses the
            // dark threshold; they neither originate a new FIT nor renew AR.
            for(bool sharedCertificate:{false,true})
            {
                auto r=RewindRecovery(464);
                auto state=ToyStoryRetainedEpisode(r);
                state.mode=NearBlackPresentationMode::RETAIN_CROP;
                const auto confirmed=ConfirmCaptionFit(r);
                auto admission=AdmitCropPresentation(r.previousAdmission,r.crop,confirmed,r.presentationEpoch).state;
                auto anchor=CommitCaptionFit(r,admission,confirmed,true);
                Assert::IsTrue(anchor.submittedEnvelopeAvailable);
                Assert::AreEqual(2000,anchor.submittedEnvelope.bottom);
                const auto nativeSequence=anchor.lastSubmittedSourceSequence;
                for(uint64_t sequence=465;sequence<=490;++sequence)
                {
                    r=RewindRecovery(sequence);
                    r.previousAdmission=admission;r.submittedScopeAnchor=anchor;
                    const bool pending=(sequence-465)%4==0;
                    r.globalNearBlack=!pending;
                    r.crop.verticalTranslationConfirmationPending=pending;
                    r.crop.verticalTranslationSourceGeneration=r.crop.frameSourceGeneration;
                    r.crop.verticalTranslationBase=r.crop.geometry;
                    auto e=EpisodeInput(r,state);
                    if(pending)e.subtitleTranslationConfirmation={132,1,sequence};
                    e.establishedScopeSubtitleRetained=pending && sharedCertificate;
                    const auto episode=EvaluateNearBlackPresentationEpisode(e);
                    r.crop.nearBlackEpisodeRetainCrop=NearBlackEpisodeOwnsCropThisFrame(episode);
                    r.candidate=Evaluate(r.crop);
                    const auto bounded=EvaluateNearBlackBoundedPresentation(episode.state,r);
                    const auto admitted=AdmitCropPresentation(admission,r.crop,bounded.presentation,r.presentationEpoch);
                    Assert::IsFalse(admitted.blocked);
                    const auto selected=ResolveUncertainPicturePresentation(r,admitted.presentation);
                    Assert::AreEqual(280,selected.presentation.sourceBounds.top);
                    Assert::AreEqual(2000,selected.presentation.sourceBounds.bottom);
                    admission=admitted.state;
                    if(selected.retained)
                    {
                        admission.outwardPresentationAvailable=selected.expanded;
                        admission.outwardPresentation=selected.presentation.sourceBounds;
                        admission.presentationSourceSequence=sequence;
                    }
                    Assert::AreEqual(1880,admission.trustedCrop.bottom);
                    anchor=CommitCaptionFit(r,admission,selected.presentation,true);
                    Assert::IsTrue(anchor.submittedEnvelopeAvailable);
                    Assert::AreEqual(nativeSequence,anchor.lastSubmittedSourceSequence);
                    state=CommitNearBlackForTest(bounded,episode.state,admission,sequence);
                }
            }
        }
        TEST_METHOD(UnsubmittedEnvelopeDoesNotChangeNormalConfirmation)
        {
            auto r=RewindRecovery(464);
            auto state=ToyStoryRetainedEpisode(r);
            state.mode=NearBlackPresentationMode::RETAIN_CROP;
            const auto confirmed=ConfirmCaptionFit(r);
            const auto admitted=AdmitCropPresentation(r.previousAdmission,r.crop,confirmed,r.presentationEpoch);
            Assert::IsFalse(admitted.blocked);
            const auto notShown=CommitCaptionFit(r,admitted.state,confirmed,false);
            Assert::IsFalse(notShown.submittedEnvelopeAvailable,
                L"A confirmed but unsubmitted candidate is not a displayed envelope.");
            r=RewindRecovery(465);r.globalNearBlack=false;r.submittedScopeAnchor=notShown;
            for(int variant=0;variant<3;++variant)
            {
                auto e=EpisodeInput(r,state);
                e.subtitleTranslationConfirmation={132,1,465};
                // Preserve the invalid-record cases: missing envelope, a
                // predicted envelope never submitted, and a native-sized box.
                e.previous.boundedPresentationAvailable=variant!=0;
                e.previous.boundedPresentation=variant==2?e.trustedCrop:confirmed.sourceBounds;
                e.previous.boundedPresentationSequence=464;
                e.previous.boundedPresentationPresentedSequence=variant==1?0:464;
                Assert::IsTrue(EvaluateNearBlackPresentationEpisode(e).ended,
                    L"Only a successfully submitted outward owner can require an envelope handoff.");
            }
        }
        TEST_METHOD(ConfirmedSubtitleAndRealRasterAuthorityCanReleaseEnvelope)
        {
            auto r=RewindRecovery(464);
            const auto confirmed=ConfirmCaptionFit(r);
            const auto admitted=AdmitCropPresentation(r.previousAdmission,r.crop,confirmed,r.presentationEpoch);
            const auto shown=CommitCaptionFit(r,admitted.state,confirmed,true);
            Assert::IsTrue(shown.submittedEnvelopeAvailable);
            r=RewindRecovery(465);r.globalNearBlack=false;
            r.submittedScopeAnchor=shown;r.previousAdmission=admitted.state;
            r.crop.verticalTranslationActive=true;r.crop.verticalTranslationPixels=142;
            r.crop.verticalTranslationBase=r.crop.geometry;
            r.crop.verticalTranslationSourceGeneration=r.crop.frameSourceGeneration;
            r.candidate=Evaluate(r.crop);
            Assert::IsTrue(r.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION);
            const auto translated=ResolveUncertainPicturePresentation(r,r.candidate);
            Assert::IsFalse(translated.retained);
            Assert::IsTrue(translated.presentation.owner==DecisionOwner::VERTICAL_TRANSLATION);
            Assert::AreEqual(142,translated.presentation.verticalTranslationPixels);
            r.crop.fullRasterPresentationAuthoritative=true;
            r.candidate=Evaluate(r.crop);
            const auto full=ResolveUncertainPicturePresentation(r,r.candidate);
            Assert::IsFalse(full.retained || full.presentation.applyCrop,
                L"An independently confirmed full raster must still replace scope.");
        }
    };

    TEST_CLASS(SubmittedEnvelopeContinuityTests)
    {
        struct Fixture
        {
            PresentationRecoveryInput input;
            NearBlackPresentationEpisodeState episode;
            PresentationRecoveryState recovery;
            CropPresentationAdmissionState admission;
            SubmittedNativeScopeAnchor anchor;

            Fixture()
            {
                input = Frame(2169);
                auto native = input.crop;
                native.latestObservationSupportsCrop = true;
                native.latestObservationIsProvisional = false;
                native.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
                native.nearBlackEpisodeRetainCrop = false;
                native.currentVisibleBoundsAvailable = false;
                admission = AdmitCropPresentation({},native,Evaluate(native),0).state;
                Assert::IsTrue(admission.available);
                auto fill = Fill(native.geometry);
                fill.trustedContentAuthorityAccepted = true;
                const auto filled = EvaluateAspectLimitFill(fill);
                Assert::IsTrue(filled.applied);
                admission.optionalFillApplied = true;
                admission.optionalFillSourceBounds = filled.sourceBounds;
                admission.optionalFillSourceSequence = 2169;
                admission.optionalFillScreenAspect = 2.35;
                Decision selected = Evaluate(native);
                selected.sourceBounds = filled.sourceBounds;
                Commit(selected,true);
                Assert::IsTrue(anchor.available);
            }
            static AspectLimitFillInput Fill(const ActivePictureBounds& bounds)
            {
                AspectLimitFillInput fill;
                fill.sourceBounds = bounds;
                fill.screenAspect = 2.35;
                fill.cropWiderContentToFillScreen = fill.widerLimitConfigured = true;
                fill.widerAspectLimit = 2.42;
                fill.cropNarrowerContentToFillScreen = fill.narrowerLimitConfigured = true;
                fill.narrowerAspectLimit = 2.32;
                return fill;
            }
            static PresentationRecoveryInput Frame(uint64_t sequence)
            {
                auto r = PendingOverlayEnvelopeTests::RewindRecovery(sequence);
                r.crop.frameSourceGeneration = r.crop.geometrySourceGeneration = 2;
                r.crop.currentVisibleSourceGeneration = r.retentionSourceGeneration = 2;
                r.presentationEpoch = 0;
                r.previousAdmission = {};
                r.submittedScopeAnchor = {};
                return r;
            }
            void Commit(const Decision& selected, bool submitted)
            {
                SubmittedNativeScopeAnchorUpdateInput update;
                update.previous = anchor;
                update.selectedAdmission = admission;
                update.nativeCropAvailable = true;
                update.nativeCrop = input.crop.geometry;
                update.submittedBounds = selected.sourceBounds;
                update.selectedOwner = selected.owner;
                update.sourceGeneration = input.crop.frameSourceGeneration;
                update.sourceSequence = input.crop.frameSourceSequence;
                update.presentationEpoch = input.presentationEpoch;
                update.frameSubmitted = submitted;
                anchor = CommitSubmittedNativeScopeAnchor(update);
            }
            Decision Run(uint64_t sequence, bool missing, bool submitted=true, bool confirmedCaptionFit=false)
            {
                input = Frame(sequence);
                if (missing)
                {
                    input.observationAvailable = false;
                    input.observation = {};
                    input.observationClassification = ActivePictureClassification::UNAVAILABLE;
                    input.crop.latestObservationClassification = ActivePictureClassification::UNAVAILABLE;
                    input.crop.latestObservationIsUnavailable = true;
                    input.crop.latestObservationIsProvisional = false;
                    input.crop.sceneVerificationHoldActive = true;
                }
                input.previousAdmission = admission;
                input.submittedScopeAnchor = anchor;
                input.previous = recovery;
                auto e = PendingOverlayEnvelopeTests::EpisodeInput(input,episode);
                e.currentObservationAvailable = input.observationAvailable;
                const auto ep = EvaluateNearBlackPresentationEpisode(e);
                episode = ep.state;
                input.crop.nearBlackEpisodeRetainCrop = NearBlackEpisodeOwnsCropThisFrame(ep);
                input.crop.nearBlackEpisodeFullRaster = episode.mode == NearBlackPresentationMode::FULL_RASTER;
                input.candidate = Evaluate(input.crop);
                if(confirmedCaptionFit)PendingOverlayEnvelopeTests::ConfirmCaptionFit(input);
                const auto recovered = EvaluatePresentationRecovery(input);
                recovery = recovered.state;
                auto boundedInput = input;
                boundedInput.previous = recovery;
                boundedInput.candidate = recovered.presentation;
                const auto bounded = EvaluateNearBlackBoundedPresentation(episode,boundedInput);
                const auto admitted = AdmitCropPresentation(admission,input.crop,bounded.presentation,0);
                admission = admitted.state;
                const auto resolved = ResolveUncertainPicturePresentation(input,admitted.presentation);
                auto selected = resolved.presentation;
                if (resolved.retained)
                {
                    input.crop.geometry = resolved.nativeCrop;
                    input.crop.geometrySourceGeneration = input.crop.frameSourceGeneration;
                    input.crop.sharedGeometryAvailable = true;
                    input.crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
                    input.crop.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
                    input.crop.latestObservationSupportsCrop = false;
                    admission.available = true;
                    admission.trustedCrop = resolved.nativeCrop;
                    admission.sourceGeneration = input.crop.frameSourceGeneration;
                    admission.presentationEpoch = input.presentationEpoch;
                    admission.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
                    admission.outwardPresentationAvailable = resolved.expanded;
                    admission.outwardPresentation = resolved.presentation.sourceBounds;
                    admission.presentationSourceSequence = sequence;
                }
                // Exercise the production fill route before committing actual output.
                FinalAspectLimitFillInput finalFill;
                finalFill.configured = Fill(selected.sourceBounds);
                finalFill.crop = input.crop;
                finalFill.presentation = selected;
                finalFill.evidence = input;
                finalFill.uncertainPicture = resolved;
                finalFill.configuredScreenActive = true;
                finalFill.recoveryBoundedPresentation = recovered.boundedPresentation;
                finalFill.nearBlackBoundedPresentation = bounded.boundedPresentation;
                finalFill.nearBlackRetainedScopeFallback = bounded.retainedScopeFallback;
                finalFill.nearBlackFailOpenProposal = bounded.failOpenProposal;
                // These frames carry the logged bottom-bar overlay, not sampling-only fill.
                finalFill.samplingCompetingPresentation = true;
                const auto filled = EvaluateFinalAspectLimitFill(finalFill);
                const auto beforeFill = selected.sourceBounds;
                admission.optionalFillApplied = filled.applied && selected.applyCrop &&
                    !selected.outwardExpanded && !selected.verticallyTranslated &&
                    !input.crop.movingPictureTransition && !recovered.state.active &&
                    input.crop.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
                    beforeFill.left == input.crop.geometry.left &&
                    beforeFill.top == input.crop.geometry.top &&
                    beforeFill.right == input.crop.geometry.right &&
                    beforeFill.bottom == input.crop.geometry.bottom;
                admission.optionalFillSourceBounds = admission.optionalFillApplied
                    ? filled.sourceBounds : ActivePictureBounds{};
                admission.optionalFillSourceSequence = sequence;
                admission.optionalFillScreenAspect = finalFill.configured.screenAspect;
                admission.samplingEnvelopeFillApplied = filled.samplingEnvelopeFilled;
                admission.samplingEnvelopeSourceBounds = filled.samplingEnvelopeFilled
                    ? beforeFill : ActivePictureBounds{};
                admission.samplingEnvelopeFillSourceBounds = filled.samplingEnvelopeFilled
                    ? filled.sourceBounds : ActivePictureBounds{};
                admission.samplingEnvelopeFillSourceSequence = sequence;
                admission.samplingEnvelopeFillScreenAspect = finalFill.configured.screenAspect;
                if (filled.applied) selected.sourceBounds = filled.sourceBounds;
                Commit(selected,submitted);
                auto actualProposal = bounded;
                actualProposal.presentation = selected;
                episode = CommitNearBlackForTest(actualProposal,episode,admission,sequence,submitted);
                return selected;
            }
        };
    public:
        TEST_METHOD(LoggedUnavailableObservationCannotContractSubmittedEnvelope)
        {
            Fixture f;
            const auto expanded = f.Run(2170,false,true,true);
            Assert::AreEqual(2000,expanded.sourceBounds.bottom);
            Assert::IsTrue(f.anchor.submittedEnvelopeAvailable, L"The envelope originated from a confirmed, actually submitted caption FIT.");
            const auto missing = f.Run(2171,true);
            Assert::AreEqual(280,missing.sourceBounds.top);
            Assert::AreEqual(2000,missing.sourceBounds.bottom,
                L"Logged2171: a missing observation must not collapse the displayed envelope to1880.");
            Assert::AreEqual(1880,f.admission.trustedCrop.bottom);
        }
        TEST_METHOD(NearBlackEnvelopeRequiresSuccessfulSelectedSubmission)
        {
            Fixture f;
            f.Run(2170,false,false,true);
            Assert::IsFalse(f.anchor.submittedEnvelopeAvailable);
            Assert::IsFalse(f.episode.boundedPresentationAvailable);
            f.Run(2171,false,true,true);
            Assert::IsTrue(f.anchor.submittedEnvelopeAvailable,
                L"The actual confirmed caption FIT must be recorded by the final continuity guard.");
            Assert::AreEqual(2000,f.anchor.submittedEnvelope.bottom);
        }
        TEST_METHOD(UnavailableCandidateNeedsMatchingSubmittedContext)
        {
            for (int variant=0; variant<7; ++variant)
            {
                Fixture f;
                f.Run(2170,false,true,true);
                f.Run(2171,true);
                auto r=f.input;
                r.submittedScopeAnchor=f.anchor;
                r.crop.currentVisibleBoundsAvailable=false;
                if (variant==0) ++r.crop.frameSourceGeneration;
                if (variant==1) ++r.presentationEpoch;
                if (variant==2) r.crop.rasterWidth=1920;
                if (variant==3) r.crop.frameSourceSequence=2169;
                if (variant==4) r.submittedScopeAnchor.submittedEnvelopeAvailable=false;
                if (variant==5) r.crop.geometry.top+=2;
                if (variant==6) r.crop.automaticCropEnabled=false;
                Decision native;
                native.applyCrop=true;
                native.owner=DecisionOwner::NEAR_BLACK_EPISODE;
                native.sourceBounds=r.crop.geometry;
                Assert::IsFalse(ResolveUncertainPicturePresentation(r,native).retained);
            }
        }
        TEST_METHOD(LargerRawExtentNeedsConfirmedCaptionOwnerBeforeItCanGrowSubmittedEnvelope)
        {
            Fixture f;
            f.Run(2170,false,true,true);
            f.Run(2171,true);
            auto r=f.input;
            r.submittedScopeAnchor=f.anchor;
            r.crop.currentVisibleBounds.bottom=2101;
            const auto raw=ResolveUncertainPicturePresentation(r,Evaluate(r.crop));
            Assert::IsTrue(raw.retained && !raw.currentPixelExtent);
            Assert::AreEqual(2000,raw.presentation.sourceBounds.bottom,
                L"A larger current rectangle alone cannot originate an expanded fallback while the subtitle owner is unknown.");
            r.crop.currentVisibleSourceSequence=2170;
            Assert::AreEqual(2000,ResolveUncertainPicturePresentation(r,Evaluate(r.crop)).presentation.sourceBounds.bottom);

            ++r.crop.frameSourceSequence;
            r.crop.currentVisibleSourceSequence=r.crop.frameSourceSequence;
            r.crop.latestObservationIsUnavailable=false;r.crop.latestObservationIsProvisional=true;
            r.crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            r.observationAvailable=true;r.observationClassification=ActivePictureClassification::PROVISIONAL;
            r.observation=r.crop.currentVisibleBounds;
            PendingOverlayEnvelopeTests::ConfirmCaptionFit(r);
            const auto confirmed=ResolveUncertainPicturePresentation(r,r.candidate);
            Assert::IsFalse(confirmed.retained);
            Assert::IsTrue(confirmed.presentation.owner==DecisionOwner::OUTWARD_FIT);
            Assert::AreEqual(2102,confirmed.presentation.sourceBounds.bottom,
                L"The independently confirmed exact current caption FIT can reveal the larger extent without waiting for AR reacquisition.");
            --r.retentionSourceSequence;
            Assert::AreEqual(2102,ResolveUncertainPicturePresentation(r,r.candidate).presentation.sourceBounds.bottom,
                L"A stale coarse AR scan cannot revoke an independently current accepted caption capsule.");
            --r.confirmedFitOwnership.sourceSequence;
            Assert::AreEqual(2000,ResolveUncertainPicturePresentation(r,r.candidate).presentation.sourceBounds.bottom,
                L"An old confirmation cannot qualify a new source envelope.");
        }
        TEST_METHOD(ConfirmedReplacementAndTranslationKeepOwnership)
        {
            Fixture f;
            f.Run(2170,false,true,true);
            f.Run(2171,true);
            auto r=f.input;
            r.submittedScopeAnchor=f.anchor;
            for (const auto owner : {DecisionOwner::TRUSTED_CROP,DecisionOwner::VERTICAL_TRANSLATION,
                DecisionOwner::ENGAGE_BASE,DecisionOwner::RELEASE_BASE,DecisionOwner::MOVING_PICTURE_HOLD})
            {
                Decision labelOnly;
                labelOnly.applyCrop=true;
                labelOnly.owner=owner;
                labelOnly.sourceBounds=r.crop.geometry;
                labelOnly.sourceBounds.top=240;
                labelOnly.sourceBounds.bottom=1920;
                labelOnly.sourceBounds.aspectRatio=3840.0/1680.0;
                const auto unsupported=ResolveUncertainPicturePresentation(r,labelOnly);
                Assert::IsTrue(unsupported.retained && unsupported.expanded,
                    L"An owner label alone cannot replace unchanged native evidence or the actually submitted caption envelope.");
                Assert::AreEqual(280,unsupported.presentation.sourceBounds.top);
                Assert::AreEqual(2000,unsupported.presentation.sourceBounds.bottom);
                Assert::AreEqual(1880,unsupported.nativeCrop.bottom);
            }

            // Publish real current native geometry before replacing the envelope.
            ++r.crop.frameSourceSequence;
            r.crop.geometry.top=240;
            r.crop.geometry.bottom=1920;
            r.crop.geometry.aspectRatio=3840.0/1680.0;
            r.crop.framePresentationEpoch=r.presentationEpoch;
            r.crop.latestObservationSupportsCrop=true;
            r.crop.latestObservationIsUnavailable=r.crop.latestObservationIsProvisional=false;
            r.crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            r.crop.nearBlackEpisodeRetainCrop=r.crop.sceneVerificationHoldActive=false;
            r.crop.currentVisibleBoundsAvailable=false;
            r.measurementCurrent=r.observationAvailable=true;
            r.observation=r.crop.geometry;
            r.observationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            r.candidate=Evaluate(r.crop);
            Assert::IsTrue(r.candidate.applyCrop && r.candidate.owner==DecisionOwner::TRUSTED_CROP);
            const auto physical=ResolveUncertainPicturePresentation(r,r.candidate);
            Assert::IsFalse(physical.retained);
            Assert::IsTrue(physical.presentation.owner==DecisionOwner::TRUSTED_CROP);
            Assert::AreEqual(240,physical.presentation.sourceBounds.top);
            Assert::AreEqual(1920,physical.presentation.sourceBounds.bottom);

            // A current confirmed translation keeps the native height and uses
            // Evaluate's actual translation contract rather than a renamed FIT.
            r=f.input;
            r.submittedScopeAnchor=f.anchor;
            ++r.crop.frameSourceSequence;
            r.crop.framePresentationEpoch=r.presentationEpoch;
            r.crop.verticalTranslationActive=true;
            r.crop.verticalTranslationPixels=96;
            r.crop.verticalTranslationBase=r.crop.geometry;
            r.crop.verticalTranslationSourceGeneration=r.crop.frameSourceGeneration;
            r.measurementCurrent=r.retentionEvaluated=r.confirmedPresentationResolved=true;
            r.retentionBounds=r.crop.geometry;
            r.retentionSourceGeneration=r.crop.frameSourceGeneration;
            r.retentionSourceSequence=r.crop.frameSourceSequence;
            r.candidate=Evaluate(r.crop);
            Assert::IsTrue(r.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION &&
                r.candidate.verticallyTranslated);
            const auto translated=ResolveUncertainPicturePresentation(r,r.candidate);
            Assert::IsFalse(translated.retained);
            Assert::IsTrue(translated.presentation.owner==DecisionOwner::VERTICAL_TRANSLATION);
            Assert::AreEqual(96,translated.presentation.verticalTranslationPixels);
            Assert::AreEqual(376,translated.presentation.sourceBounds.top);
            Assert::AreEqual(1976,translated.presentation.sourceBounds.bottom);
            Assert::AreEqual(r.crop.geometry.bottom-r.crop.geometry.top,
                translated.presentation.sourceBounds.bottom-translated.presentation.sourceBounds.top);

            r.crop.fullRasterPresentationAuthoritative=true;
            const auto full=ResolveUncertainPicturePresentation(r,Evaluate(r.crop));
            Assert::IsFalse(full.retained || full.presentation.applyCrop,
                L"Independent full-raster authority must still replace the submitted envelope.");
        }
        TEST_METHOD(TranslatedSubmissionRetiresEnvelopeWithoutRenewingNativeAR)
        {
            Fixture f;
            f.Run(2170,false,true,true);
            const auto nativeSequence=f.anchor.lastSubmittedSourceSequence;
            f.input.crop.frameSourceSequence=2180;
            Decision translated;
            translated.applyCrop=translated.verticallyTranslated=true;
            translated.owner=DecisionOwner::VERTICAL_TRANSLATION;
            translated.sourceBounds=f.input.crop.geometry;
            translated.sourceBounds.top+=130;
            translated.sourceBounds.bottom+=130;
            f.Commit(translated,false);
            Assert::IsTrue(f.anchor.submittedEnvelopeAvailable);
            f.Commit(translated,true);
            Assert::IsFalse(f.anchor.submittedEnvelopeAvailable);
            Assert::AreEqual(nativeSequence,f.anchor.lastSubmittedSourceSequence);
            Assert::AreEqual(uint64_t(2180),f.anchor.lastSubmittedPresentationSourceSequence);
            // A delayed source cannot resurrect the retired envelope, or clear
            // this anchor with older full-raster authority.
            f.input.crop.frameSourceSequence=2171;
            translated.owner=DecisionOwner::NEAR_BLACK_EPISODE;
            translated.sourceBounds=f.input.crop.geometry;
            translated.sourceBounds.bottom=2000;
            f.Commit(translated,true);
            Assert::IsFalse(f.anchor.submittedEnvelopeAvailable);
            SubmittedNativeScopeAnchorUpdateInput late;
            late.previous=f.anchor;
            late.frameSubmitted=late.fullRasterAuthority=true;
            late.sourceGeneration=2;
            late.sourceSequence=2172;
            late.presentationEpoch=0;
            Assert::IsTrue(CommitSubmittedNativeScopeAnchor(late).available);
        }    };
TEST_CLASS(FinalAspectLimitFillRoutingTests)
{
    static FinalAspectLimitFillInput Frame(uint64_t sequence, int observedTop,
        int observedLeft = 1068, int observedBottom = 1600)
    {
        FinalAspectLimitFillInput input;
        input.crop = TrustedScopeCrop();
        input.crop.frameSourceGeneration = input.crop.geometrySourceGeneration = 2;
        input.crop.frameSourceSequence = sequence;
        input.crop.framePresentationEpoch = 0;
        input.crop.geometry = {0,280,3840,1880,3840,2160,2.4,
            ActivePictureBounds::BarAxes::TOP_BOTTOM};
        input.crop.latestObservationSupportsCrop = false;
        input.crop.latestObservationIsProvisional = true;
        input.crop.latestObservationClassification = ActivePictureClassification::PROVISIONAL;
        input.crop.frameLocalPresentationRetentionEvaluated = true;
        input.crop.frameLocalPresentationRetentionSafe = observedTop == 280;
        input.crop.presentationFailOpen = observedTop == 278;
        auto& evidence = input.evidence;
        evidence.crop = input.crop;
        evidence.measurementCurrent = evidence.retentionEvaluated = true;
        evidence.excludedBandsPixelSafe = evidence.observationAvailable = true;
        evidence.nearBlackEvaluated = true;
        evidence.retentionSourceGeneration = 2;
        evidence.retentionSourceSequence = sequence;
        evidence.retentionBounds = input.crop.geometry;
        evidence.observationClassification = ActivePictureClassification::PROVISIONAL;
        evidence.observation = {observedLeft,observedTop,3840,observedBottom,
            3840,2160,0,ActivePictureBounds::BarAxes::NONE};
        evidence.submittedScopeAnchor.available = true;
        evidence.submittedScopeAnchor.crop = input.crop.geometry;
        evidence.submittedScopeAnchor.sourceGeneration = 2;
        evidence.submittedScopeAnchor.presentationEpoch = 0;
        evidence.submittedScopeAnchor.lastSubmittedSourceSequence = 953;
        input.configuredScreenActive = true;
        input.samplingCompetingPresentation = false;
        input.configured.screenAspect = 2.35;
        input.configured.cropNarrowerContentToFillScreen = true;
        input.configured.narrowerLimitConfigured = true;
        input.configured.narrowerAspectLimit = 2.32;
        input.configured.cropWiderContentToFillScreen = true;
        input.configured.widerLimitConfigured = true;
        input.configured.widerAspectLimit = 2.42;
        return input;
    }
    static void Hold(FinalAspectLimitFillInput& input, const ActivePictureBounds& envelope)
    {
        input.presentation.sourceBounds = envelope;
        input.presentation.applyCrop = true;
        input.presentation.outwardExpanded = true;
        input.presentation.owner = DecisionOwner::OUTWARD_FIT;
        input.recoveryBoundedPresentation = true;
        // Explicitly exercise the path that an uncertainty-only patch misses.
        input.uncertainPicture = {};
    }
public:
    TEST_METHOD(Logged954955991992KeepsFillThroughRecoveryHold)
    {
        using namespace AnamorphicPresentation;
        auto first = Frame(954,278);
        first.uncertainPicture = ResolveUncertainPicturePresentation(
            first.evidence, Evaluate(first.evidence.crop));
        first.presentation = first.uncertainPicture.presentation;
        Assert::IsTrue(first.uncertainPicture.expanded);
        ActivePictureBounds heldEnvelope = first.presentation.sourceBounds;
        for (uint64_t sequence=954; sequence<=992; ++sequence)
        {
            auto input = sequence == 954 ? first : Frame(sequence,280);
            if (sequence > 954 && sequence < 991)
                Hold(input, heldEnvelope);
            if (sequence == 991)
            {
                input = Frame(sequence,280,1056,1588);
                input.presentation = Evaluate(input.crop);
            }
            if (sequence == 992)
            {
                input = Frame(sequence,278,1056,1586);
                input.uncertainPicture = ResolveUncertainPicturePresentation(
                    input.evidence, Evaluate(input.evidence.crop));
                input.presentation = input.uncertainPicture.presentation;
            }
            const auto selected = EvaluateFinalAspectLimitFill(input);
            Assert::IsTrue(selected.applied);
            Assert::AreEqual(sequence == 991 ? 40 : 38, selected.sourceBounds.left);
            Assert::AreEqual(sequence == 991 ? 3800 : 3802, selected.sourceBounds.right);
            Assert::AreEqual(sequence == 991 ? 280 : 278, selected.sourceBounds.top);
            Assert::AreEqual(1880, selected.sourceBounds.bottom);
            Assert::AreEqual(280, input.crop.geometry.top);
            Assert::AreEqual(1880, input.crop.geometry.bottom);
            const auto source = ResolveNlsSourceGeometry(true, selected.sourceBounds.left,
                selected.sourceBounds.top, selected.sourceBounds.right,
                selected.sourceBounds.bottom,3840,2160);
            const auto screen = FitAspect(2.35,{0,0,3840,2160},VerticalPictureAlignment::CENTER);
            const auto layout = FitAspect(source.aspect,screen.picture,VerticalPictureAlignment::CENTER);
            Assert::IsTrue(source.valid && layout.valid);
            Assert::AreEqual(screen.picture.top,layout.picture.top,0.001);
            Assert::AreEqual(screen.picture.bottom,layout.picture.bottom,0.001);
            // 3764 / 1602 differs from2.35 only by floor-to-even rounding.
            const double expectedWidth = (screen.picture.bottom-screen.picture.top)*source.aspect;
            Assert::AreEqual(expectedWidth,layout.picture.right-layout.picture.left,0.001);
        }
    }
    TEST_METHOD(LogDerivedPartialRowsKeepFillThroughMeasuredRecoveryAndReentry)
    {
        using namespace AnamorphicPresentation;
        // Derived from sources 954 / 991 / 992. The intervening safe sources are
        // synthetic: they earn the real recovery proof, rather than reproducing
        // every unlogged detector sample or assigning a recovery sample counter.
        PresentationRecoveryState recovery;
        CropPresentationAdmissionState admission;
        SubmittedNativeScopeAnchor anchor;
        struct Sample
        {
            Decision actual;
            PresentationRecoveryDecision recovered;
            AspectLimitFillDecision filled;
        };
        auto submit = [&](FinalAspectLimitFillInput routing) -> Sample
        {
            auto& evidence = routing.evidence;
            evidence.crop = routing.crop;
            evidence.previous = recovery;
            evidence.previousAdmission = admission;
            evidence.submittedScopeAnchor = anchor;
            evidence.framesPerSecond = 24.0;
            evidence.presentationEpoch = routing.crop.framePresentationEpoch;
            evidence.currentTick = 1000 + (routing.crop.frameSourceSequence - 953) * 42;
            evidence.candidate = Evaluate(evidence.crop);
            const auto recovered = EvaluatePresentationRecovery(evidence);
            recovery = recovered.state;
            auto boundedInput = evidence;
            boundedInput.previous = recovery;
            boundedInput.candidate = recovered.presentation;
            const auto bounded = EvaluateNearBlackBoundedPresentation({},boundedInput);
            auto admitted = AdmitCropPresentation(admission,routing.crop,
                bounded.presentation,evidence.presentationEpoch);
            const auto resolved = ResolveUncertainPicturePresentation(evidence,admitted.presentation);
            if (resolved.retained)
            {
                routing.crop.geometry = resolved.nativeCrop;
                routing.crop.geometrySourceGeneration = routing.crop.frameSourceGeneration;
                routing.crop.sharedGeometryAvailable = true;
                routing.crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
                routing.crop.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
                routing.crop.latestObservationSupportsCrop = false;
                admitted.blocked = false;
                admitted.presentation = resolved.presentation;
                admitted.state.available = true;
                admitted.state.trustedCrop = resolved.nativeCrop;
                admitted.state.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
                admitted.state.sourceGeneration = routing.crop.frameSourceGeneration;
                admitted.state.presentationEpoch = evidence.presentationEpoch;
                admitted.state.outwardPresentationAvailable = resolved.expanded;
                admitted.state.outwardPresentation = resolved.presentation.sourceBounds;
                admitted.state.presentationSourceSequence = routing.crop.frameSourceSequence;
            }
            Assert::IsFalse(admitted.blocked);
            admission = admitted.state;
            routing.presentation = admitted.presentation;
            routing.uncertainPicture = resolved;
            routing.recoveryBoundedPresentation = recovered.boundedPresentation;
            routing.nearBlackBoundedPresentation = bounded.boundedPresentation;
            routing.nearBlackRetainedScopeFallback = bounded.retainedScopeFallback;
            routing.nearBlackFailOpenProposal = bounded.failOpenProposal;
            const auto filled = EvaluateFinalAspectLimitFill(routing);
            auto actual = routing.presentation;
            if (filled.applied) actual.sourceBounds = filled.sourceBounds;
            const auto& base = routing.crop.geometry;
            const auto& envelope = routing.presentation.sourceBounds;
            admission.optionalFillApplied = filled.applied && routing.presentation.applyCrop &&
                !routing.presentation.outwardExpanded && !routing.presentation.verticallyTranslated &&
                !routing.crop.movingPictureTransition && !recovered.state.active &&
                routing.crop.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
                envelope.left == base.left && envelope.top == base.top &&
                envelope.right == base.right && envelope.bottom == base.bottom;
            admission.optionalFillSourceBounds = admission.optionalFillApplied
                ? filled.sourceBounds : ActivePictureBounds{};
            admission.optionalFillSourceSequence = routing.crop.frameSourceSequence;
            admission.optionalFillScreenAspect = routing.configured.screenAspect;
            admission.samplingEnvelopeFillApplied = filled.samplingEnvelopeFilled;
            admission.samplingEnvelopeSourceBounds = filled.samplingEnvelopeFilled
                ? envelope : ActivePictureBounds{};
            admission.samplingEnvelopeFillSourceBounds = filled.samplingEnvelopeFilled
                ? filled.sourceBounds : ActivePictureBounds{};
            admission.samplingEnvelopeFillSourceSequence = routing.crop.frameSourceSequence;
            admission.samplingEnvelopeFillScreenAspect = routing.configured.screenAspect;
            SubmittedNativeScopeAnchorUpdateInput commit;
            commit.previous = anchor;
            commit.selectedAdmission = admission;
            commit.nativeCropAvailable = admission.available;
            commit.nativeCrop = admission.trustedCrop;
            commit.authorityOrigin = admission.authorityOrigin;
            commit.submittedBounds = actual.sourceBounds;
            commit.selectedOwner = actual.owner;
            commit.sourceGeneration = routing.crop.frameSourceGeneration;
            commit.sourceSequence = routing.crop.frameSourceSequence;
            commit.presentationEpoch = evidence.presentationEpoch;
            commit.fullRasterAuthority = routing.crop.fullRasterPresentationAuthoritative;
            commit.frameSubmitted = true;
            anchor = CommitSubmittedNativeScopeAnchor(commit);
            Assert::IsTrue(anchor.available);
            Assert::AreEqual(280,admission.trustedCrop.top);
            Assert::AreEqual(1880,admission.trustedCrop.bottom);
            Assert::AreEqual(280,anchor.crop.top);
            Assert::AreEqual(1880,anchor.crop.bottom);
            return {actual,recovered,filled};
        };
        const auto screen = FitAspect(2.35,{0,0,3840,2160},VerticalPictureAlignment::CENTER);
        auto assertFilledLayout = [&](const Sample& sample, bool envelope)
        {
            Assert::IsTrue(sample.filled.applied);
            Assert::AreEqual(envelope ? 38 : 40,sample.actual.sourceBounds.left);
            Assert::AreEqual(envelope ? 3802 : 3800,sample.actual.sourceBounds.right);
            Assert::AreEqual(envelope ? 278 : 280,sample.actual.sourceBounds.top);
            Assert::AreEqual(1880,sample.actual.sourceBounds.bottom);
            const auto source = ResolveNlsSourceGeometry(true,sample.actual.sourceBounds.left,
                sample.actual.sourceBounds.top,sample.actual.sourceBounds.right,
                sample.actual.sourceBounds.bottom,3840,2160);
            const auto layout = FitAspect(source.aspect,screen.picture,VerticalPictureAlignment::CENTER);
            Assert::IsTrue(source.valid && layout.valid);
            Assert::AreEqual(screen.picture.top,layout.picture.top,0.001);
            Assert::AreEqual(screen.picture.bottom,layout.picture.bottom,0.001);
            Assert::IsTrue(std::abs((layout.picture.right-layout.picture.left) -
                (screen.picture.right-screen.picture.left)) < 1.0,
                L"Chroma alignment may change width by less than one pixel; fill must not toggle off.");
        };
        // Obtain admission and the native anchor from a successfully submitted,
        // actually filled source. Frame()'s synthetic anchor is never borrowed.
        auto seed = Frame(953,280);
        seed.crop.latestObservationSupportsCrop = true;
        seed.crop.latestObservationIsProvisional = false;
        seed.crop.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
        seed.evidence.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
        seed.evidence.observation = seed.crop.geometry;
        seed.evidence.observedTrustedCrop = seed.crop.geometry;
        assertFilledLayout(submit(seed),false);
        Assert::AreEqual(uint64_t(953),anchor.lastSubmittedSourceSequence);
        const auto first = submit(Frame(954,278));
        assertFilledLayout(first,true);
        Assert::IsTrue(first.recovered.started && first.recovered.state.active);
        Assert::AreEqual(0u,first.recovered.samples);
        Assert::IsTrue(anchor.submittedEnvelopeAvailable);
        Assert::AreEqual(0,anchor.submittedEnvelope.left);
        Assert::AreEqual(3840,anchor.submittedEnvelope.right);
        Assert::AreEqual(278,anchor.submittedEnvelope.top);
        Assert::AreEqual(uint64_t(953),anchor.lastSubmittedSourceSequence,
            L"A filled sampling envelope must not renew native AR authority.");
        uint64_t sequence = 955;
        bool released = false;
        unsigned safeSources = 0;
        for (; sequence < 980; ++sequence)
        {
            const auto sample = submit(Frame(sequence,280,1056,1588));
            ++safeSources;
            Assert::AreEqual(safeSources,sample.recovered.samples);
            released = sample.recovered.released;
            assertFilledLayout(sample,!released);
            if (released)
            {
                Assert::AreEqual(sample.recovered.required,sample.recovered.samples);
                Assert::IsFalse(sample.recovered.state.active);
                Assert::IsFalse(anchor.submittedEnvelopeAvailable);
                Assert::AreEqual(sequence,anchor.lastSubmittedSourceSequence);
                break;
            }
            Assert::IsTrue(sample.recovered.state.active);
            Assert::IsTrue(anchor.submittedEnvelopeAvailable);
            Assert::AreEqual(278,anchor.submittedEnvelope.top);
            Assert::AreEqual(uint64_t(953),anchor.lastSubmittedSourceSequence);
        }
        Assert::IsTrue(released,L"Distinct current safe observations must earn recovery release.");
        Assert::IsTrue(safeSources > 1);
        const auto nativeSequence = anchor.lastSubmittedSourceSequence;
        const auto reentry = submit(Frame(sequence+1,278,1056,1586));
        assertFilledLayout(reentry,true);
        Assert::IsTrue(reentry.recovered.started && reentry.recovered.state.active);
        Assert::AreEqual(0u,reentry.recovered.samples);
        Assert::IsTrue(anchor.submittedEnvelopeAvailable);
        Assert::AreEqual(nativeSequence,anchor.lastSubmittedSourceSequence);
        Assert::AreEqual(sequence+1,anchor.submittedEnvelopeSourceSequence);
    }
    TEST_METHOD(SamplingEnvelopeCommitNeedsExactSelectedFillCertificate)
    {
        auto route=Frame(954,278);
        route.uncertainPicture=ResolveUncertainPicturePresentation(route.evidence,Evaluate(route.crop));
        route.presentation=route.uncertainPicture.presentation;
        const auto filled=EvaluateFinalAspectLimitFill(route);
        Assert::IsTrue(filled.samplingEnvelopeFilled);
        SubmittedNativeScopeAnchorUpdateInput valid;
        valid.previous=route.evidence.submittedScopeAnchor;
        valid.nativeCropAvailable=valid.frameSubmitted=true;
        valid.nativeCrop=route.crop.geometry;
        valid.sourceGeneration=2;
        valid.sourceSequence=954;
        valid.presentationEpoch=0;
        valid.selectedOwner=DecisionOwner::OUTWARD_FIT;
        valid.submittedBounds=filled.sourceBounds;
        auto& selected=valid.selectedAdmission;
        selected.available=true;
        selected.trustedCrop=valid.nativeCrop;
        selected.sourceGeneration=2;
        selected.samplingEnvelopeFillApplied=true;
        selected.samplingEnvelopeSourceBounds=route.presentation.sourceBounds;
        selected.samplingEnvelopeFillSourceBounds=filled.sourceBounds;
        selected.samplingEnvelopeFillSourceSequence=954;
        selected.samplingEnvelopeFillScreenAspect=2.35;
        const auto committed=CommitSubmittedNativeScopeAnchor(valid);
        Assert::IsTrue(committed.submittedEnvelopeAvailable);
        Assert::AreEqual(278,committed.submittedEnvelope.top);
        Assert::AreEqual(uint64_t(953),committed.lastSubmittedSourceSequence);
        for(int variant=0;variant<12;++variant)
        {
            auto bad=valid;
            if(variant==0) bad.frameSubmitted=false;
            if(variant==1) bad.selectedAdmission.samplingEnvelopeFillApplied=false;
            if(variant==2) --bad.selectedAdmission.samplingEnvelopeFillSourceSequence;
            if(variant==3) ++bad.selectedAdmission.sourceGeneration;
            if(variant==4) ++bad.selectedAdmission.presentationEpoch;
            if(variant==5) bad.submittedBounds.top-=2;
            if(variant==6) bad.submittedBounds.left+=2;
            if(variant==7) bad.selectedAdmission.samplingEnvelopeSourceBounds.top=270;
            if(variant==8) bad.selectedAdmission.samplingEnvelopeFillScreenAspect=2.34;
            if(variant==9) bad.selectedAdmission.trustedCrop.top=282;
            if(variant==10) bad.previous={};
            if(variant==11) bad.sourceSequence=952;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(bad).submittedEnvelopeAvailable);
        }
    }    TEST_METHOD(CurrentProofLimitsAndKnownOwnersVetoHeldSamplingFill)
    {
        for (int variant=0; variant<15; ++variant)
        {
            auto input=Frame(955,280);
            Hold(input,{0,278,3840,1880,3840,2160,0,ActivePictureBounds::BarAxes::NONE});
            if(variant==0) input.evidence.excludedBandsPixelSafe=false;
            if(variant==1) input.evidence.measurementCurrent=false;
            if(variant==2) --input.evidence.retentionSourceSequence;
            if(variant==3) ++input.evidence.submittedScopeAnchor.presentationEpoch;
            if(variant==4) ++input.crop.geometrySourceGeneration;
            if(variant==5) input.samplingCompetingPresentation=true; // Includes Classic icons.
            if(variant==6) input.evidence.globalNearBlack=true;
            if(variant==7) input.crop.currentVisibleBoundsAvailable=true;
            if(variant==8) input.configured.cropWiderContentToFillScreen=false;
            if(variant==9) input.configured.widerAspectLimit=2.39; // Envelope is below native2.4.
            if(variant==10) input.configured.screenAspect=2.398; // Would switch to vertical fill.
            if(variant==11) input.presentation.sourceBounds.top=274; // Beyond existing step4.
            if(variant==12) input.crop.verticalTranslationConfirmationPending=true;
            if(variant==13) input.nlsOwnsPresentationGeometry=true;
            if(variant==14) input.protectedCaptionFit=true;
            const auto selected=EvaluateFinalAspectLimitFill(input);
            Assert::IsFalse(selected.applied);
            Assert::AreEqual(input.presentation.sourceBounds.top,selected.sourceBounds.top);
            Assert::AreEqual(0,selected.sourceBounds.left);
            Assert::AreEqual(3840,selected.sourceBounds.right);
        }
    }
};


TEST_CLASS(AdvancedPhysicalScopeFillAdmissionTests)
{
    struct Fixture
    {
        FinalAspectLimitFillInput route;
        Decision shared;
        AspectLimitFillDecision fill;
        CropPresentationAdmissionState admission;
        SubtitlePreviewMode mode;
        bool captionHeld=false,fixedCrop=false,nls=false,protectedCaption=false,recovery=false;
        ActivePictureAuthorityOrigin origin=ActivePictureAuthorityOrigin::NATIVE;

        Fixture(SubtitlePreviewMode selectedMode=SubtitlePreviewMode::GeneratedGrayBackground,
            bool prepared=true,bool confirmedBlank=false) : mode(selectedMode)
        {
            route.crop=TrustedScopeCrop();
            auto& crop=route.crop;
            crop.frameSourceGeneration=crop.geometrySourceGeneration=2;
            crop.frameSourceSequence=385;crop.framePresentationEpoch=0;
            crop.verticalTranslationActive=true;crop.verticalTranslationPixels=182;
            crop.verticalTranslationBase=crop.geometry;crop.verticalTranslationSourceGeneration=2;
            shared=Evaluate(crop);
            Assert::IsTrue(shared.applyCrop && shared.verticallyTranslated);
            Assert::AreEqual(int(DecisionOwner::VERTICAL_TRANSLATION),int(shared.owner));
            Assert::AreEqual(182,shared.verticalTranslationPixels);
            const auto admitted=AdmitCropPresentation({},crop,shared,0);
            Assert::IsFalse(admitted.blocked);
            Assert::IsTrue(admitted.state.available);
            admission=admitted.state;
            route.presentation=admitted.presentation;
            route.presentation.sourceBounds=SubtitlePresentationBounds(
                admitted.presentation,mode,prepared,confirmedBlank);
            route.configuredScreenActive=true;
            route.configured.screenAspect=2.35;
            route.configured.cropWiderContentToFillScreen=route.configured.widerLimitConfigured=true;
            route.configured.widerAspectLimit=2.42;
            route.configured.cropNarrowerContentToFillScreen=route.configured.narrowerLimitConfigured=true;
            route.configured.narrowerAspectLimit=2.32;
            RefreshFillRecord();
        }
        bool RecordEligible() const
        {
            return CanRecordSubtitleOptionalAspectFill(route.crop,route.presentation,fill,mode,
                captionHeld,fixedCrop,nls,protectedCaption,recovery,origin);
        }
        void RefreshFillRecord()
        {
            fill=EvaluateFinalAspectLimitFill(route);
            admission.optionalFillApplied=RecordEligible();
            admission.optionalFillSourceBounds=admission.optionalFillApplied?fill.sourceBounds:ActivePictureBounds{};
            admission.optionalFillSourceSequence=route.crop.frameSourceSequence;
            admission.optionalFillScreenAspect=route.configured.screenAspect;
        }
        SubmittedNativeScopeAnchorUpdateInput CommitInput() const
        {
            SubmittedNativeScopeAnchorUpdateInput commit;
            commit.selectedAdmission=admission;
            commit.nativeCrop=route.crop.geometry;commit.submittedBounds=fill.sourceBounds;
            commit.nativeCropAvailable=commit.frameSubmitted=true;
            commit.authorityOrigin=origin;commit.selectedOwner=route.presentation.owner;
            commit.sourceGeneration=route.crop.frameSourceGeneration;
            commit.sourceSequence=route.crop.frameSourceSequence;
            commit.presentationEpoch=route.crop.framePresentationEpoch;
            return commit;
        }
    };
public:
    TEST_METHOD(AdvancedPhysicalTranslationRecordsConfiguredFillAndAcquiresSubmittedScope)
    {
        for(auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground})
            for(bool prepared:{false,true})for(bool confirmedBlank:{false,true}) {
                Fixture f(mode,prepared,confirmedBlank);
                Assert::IsTrue(f.fill.applied);
                Assert::IsTrue(f.route.presentation.verticallyTranslated,
                    L"Shared policy retains the logical translation owner after physical projection.");
                Assert::AreEqual(274,f.route.presentation.sourceBounds.top);
                Assert::AreEqual(1884,f.route.presentation.sourceBounds.bottom);
                Assert::AreEqual(28,f.fill.sourceBounds.left);
                Assert::AreEqual(3810,f.fill.sourceBounds.right);
                Assert::IsTrue(f.admission.optionalFillApplied,
                    L"Actual horizontal fill of the exact physical native crop needs a selected fill witness.");
                const auto committed=CommitSubmittedNativeScopeAnchor(f.CommitInput());
                Assert::IsTrue(IsCurrentSubmittedNativeScopeAnchor(committed,2,386,0,3840,2160));
                Assert::AreEqual(0,committed.crop.left);
                Assert::AreEqual(3840,committed.crop.right);
                Assert::AreEqual(uint64_t(385),committed.lastSubmittedSourceSequence);
            }
    }
    TEST_METHOD(AdvancedFillRecorderPreservesEveryExistingVetoAndRejectsClassicTranslation)
    {
        for(int scenario=0;scenario<15;++scenario) {
            Fixture f;
            switch(scenario) {
            case 0: f.captionHeld=true;break;
            case 1: f.fixedCrop=true;break;
            case 2: f.nls=true;break;
            case 3: f.protectedCaption=true;break;
            case 4: f.route.presentation.outwardExpanded=true;break;
            case 5: f.route.crop.movingPictureTransition=true;break;
            case 6: f.recovery=true;break;
            case 7: f.origin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;break;
            case 8: f.fill.applied=false;break;
            case 9: f.route.presentation.applyCrop=false;break;
            case 10: f.mode=SubtitlePreviewMode::None;f.route.presentation=f.shared;break;
            case 11: f.mode=SubtitlePreviewMode::GreenBox;f.route.presentation=f.shared;break;
            case 12: f.route.presentation.sourceBounds.top+=2;f.route.presentation.sourceBounds.bottom+=2;break;
            case 13: ++f.route.presentation.sourceBounds.rasterWidth;break;
            case 14: f.route.presentation.owner=DecisionOwner::OUTWARD_FIT;break;
            }
            Assert::IsFalse(f.RecordEligible());
        }
        Fixture classic(SubtitlePreviewMode::None);
        Assert::AreEqual(456,classic.route.presentation.sourceBounds.top);
        Assert::AreEqual(2066,classic.route.presentation.sourceBounds.bottom);
        Assert::IsFalse(classic.admission.optionalFillApplied);
        Assert::IsFalse(CommitSubmittedNativeScopeAnchor(classic.CommitInput()).available,
            L"Same-aspect genuine Classic translation must never acquire a native anchor.");
        // Preserve the original normal native-crop recorder path.
        Fixture ordinary(SubtitlePreviewMode::None);
        ordinary.route.crop.verticalTranslationActive=false;
        ordinary.route.presentation=Evaluate(ordinary.route.crop);
        ordinary.RefreshFillRecord();
        Assert::IsTrue(ordinary.admission.optionalFillApplied);
    }
    TEST_METHOD(AdvancedPhysicalFillHonorsUserLimitsAndExactSelectedSubmission)
    {
        for(int scenario=0;scenario<2;++scenario) {
            Fixture f;
            if(scenario==0)f.route.configured.cropWiderContentToFillScreen=false;
            if(scenario==1)f.route.configured.widerAspectLimit=2.38;
            f.RefreshFillRecord();
            Assert::IsFalse(f.fill.applied);
            Assert::IsFalse(f.admission.optionalFillApplied);
        }
        for(int scenario=0;scenario<8;++scenario) {
            Fixture f;auto commit=f.CommitInput();
            switch(scenario) {
            case 0: ++commit.selectedAdmission.optionalFillSourceSequence;break;
            case 1: ++commit.selectedAdmission.sourceGeneration;break;
            case 2: ++commit.selectedAdmission.presentationEpoch;break;
            case 3: commit.frameSubmitted=false;break;
            case 4: commit.fullRasterAuthority=true;break;
            case 5: commit.selectedAdmission.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;break;
            case 6: commit.submittedBounds.top+=2;commit.submittedBounds.bottom+=2;
                commit.selectedAdmission.optionalFillSourceBounds=commit.submittedBounds;break;
            case 7: commit.selectedAdmission.trustedCrop.top+=2;break;
            }
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(commit).available);
        }
    }
    TEST_METHOD(LoggedAdvancedPhysicalFillAnchorsNearBlack386AndPreservesRealOutwardContent)
    {
        Fixture f;
        const auto anchor=CommitSubmittedNativeScopeAnchor(f.CommitInput());
        Assert::IsTrue(anchor.available,
            L"The anchor starts empty; its only certificate must come from the actual renderer recording predicate.");
        const auto scope=f.route.crop.geometry;
        NearBlackPresentationEpisodeInput episode;
        episode.sourceGeneration=2;episode.sourceSequence=386;episode.presentationEpoch=0;
        episode.measurementCurrent=episode.nearBlackEvaluated=episode.globalNearBlack=true;
        episode.trustedCropAvailable=true;episode.trustedCrop=scope;
        episode.previouslyAdmittedCropAvailable=IsCurrentSubmittedNativeScopeAnchor(anchor,2,386,0,3840,2160);
        episode.previouslyAdmittedCrop=anchor.crop;
        episode.previouslyAdmittedCropSourceGeneration=anchor.sourceGeneration;
        episode.previouslyAdmittedCropPresentationEpoch=anchor.presentationEpoch;
        episode.boundedVisibleContentOutsideCrop=true;
        episode.retentionEvaluated=true;episode.retentionSafe=false;
        episode.retentionBounds=scope;episode.retentionSourceGeneration=2;episode.retentionSourceSequence=386;
        episode.currentObservationAvailable=true;episode.currentObservation=scope;
        episode.currentObservation.top=266;episode.currentObservation.bottom=1892;
        episode.currentVisibleBoundsAvailable=true;episode.currentVisibleBounds=scope;
        // The logged coarse rectangle 266..2024 is raw occupancy only until a
        // separate current caption/FIT owner confirms that presentation.
        episode.currentVisibleBounds.top=266;episode.currentVisibleBounds.bottom=2024;
        episode.currentVisibleSourceGeneration=2;episode.currentVisibleSourceSequence=386;
        const auto entered=EvaluateNearBlackPresentationEpisode(episode);
        Assert::IsTrue(entered.started);
        Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(entered.state.mode));
        Assert::IsFalse(entered.changedToFullRaster);

        // First retain native fill while the caption is pending; then prove a
        // current caption FIT can expose its complete owned envelope.
        auto pending=ToyStoryNearBlackRecovery(386);
        pending.presentationEpoch=pending.crop.framePresentationEpoch=0;
        pending.crop.frameSourceGeneration=pending.crop.geometrySourceGeneration=2;
        pending.crop.geometry=pending.crop.currentVisibleBase=scope;
        pending.crop.currentVisibleSourceGeneration=pending.retentionSourceGeneration=2;
        pending.crop.currentVisibleBounds=episode.currentVisibleBounds;
        pending.crop.nearBlackEpisodeFullRaster=false;pending.crop.nearBlackEpisodeRetainCrop=true;
        pending.crop.verticalTranslationConfirmationPending=true;
        pending.retentionBounds=scope;pending.observation=episode.currentObservation;
        pending.previousAdmission=f.admission;pending.submittedScopeAnchor=anchor;
        pending.globalNearBlack=true;pending.candidate=Evaluate(pending.crop);
        const auto& visible=pending.crop.currentVisibleBounds;
        Assert::IsTrue(visible.left<=pending.observation.left && visible.top<=pending.observation.top &&
            visible.right>=pending.observation.right && visible.bottom>=pending.observation.bottom,
            L"The raw occupied extent contains the complete current observation.");
        Assert::IsTrue(visible.left<=scope.left && visible.top<=scope.top &&
            visible.right>=scope.right && visible.bottom>=scope.bottom,
            L"The raw occupied extent also contains the unchanged native picture.");
        const auto raw=EvaluateNearBlackBoundedPresentation(entered.state,pending);
        const auto rawAdmitted=AdmitCropPresentation(f.admission,pending.crop,raw.presentation,0);
        Assert::IsFalse(rawAdmitted.blocked);
        Assert::IsTrue(rawAdmitted.presentation.applyCrop);
        Assert::AreEqual(scope.top,rawAdmitted.presentation.sourceBounds.top,
            L"Pending raw occupancy must not originate a larger presentation.");
        Assert::AreEqual(scope.bottom,rawAdmitted.presentation.sourceBounds.bottom);
        Assert::IsFalse(raw.boundedPresentation);
        Assert::IsFalse(anchor.submittedEnvelopeAvailable);

        PendingOverlayEnvelopeTests::ConfirmCaptionFit(pending);
        const auto bounded=EvaluateNearBlackBoundedPresentation(entered.state,pending);
        Assert::AreEqual(int(DecisionOwner::OUTWARD_FIT),int(bounded.presentation.owner));
        Assert::AreEqual(266,bounded.presentation.sourceBounds.top,
            L"The independently confirmed caption FIT must retain its entire current envelope.");
        Assert::AreEqual(2024,bounded.presentation.sourceBounds.bottom);
        const auto admitted=AdmitCropPresentation(f.admission,pending.crop,bounded.presentation,0);
        Assert::IsFalse(admitted.blocked);
        Assert::IsTrue(admitted.presentation.applyCrop);
        Assert::IsTrue(admitted.presentation.sourceBounds.top<=266);
        Assert::IsTrue(admitted.presentation.sourceBounds.bottom>=2024);
        Assert::IsTrue(admitted.presentation.sourceBounds.top>0 && admitted.presentation.sourceBounds.bottom<2160);
        Assert::AreEqual(274,admitted.state.trustedCrop.top);
        Assert::AreEqual(1884,admitted.state.trustedCrop.bottom);
        const auto captionShown=PendingOverlayEnvelopeTests::CommitCaptionFit(
            pending,admitted.state,admitted.presentation,true);
        Assert::IsTrue(captionShown.submittedEnvelopeAvailable);
        Assert::AreEqual(266,captionShown.submittedEnvelope.top);
        Assert::AreEqual(2024,captionShown.submittedEnvelope.bottom);
        Assert::AreEqual(anchor.lastSubmittedSourceSequence,captionShown.lastSubmittedSourceSequence,
            L"The selected caption envelope must not renew native AR authority.");
        auto outwardFill=f.route;outwardFill.crop=pending.crop;outwardFill.presentation=admitted.presentation;
        outwardFill.nearBlackBoundedPresentation=bounded.boundedPresentation;
        const auto preserved=EvaluateFinalAspectLimitFill(outwardFill);
        Assert::IsFalse(preserved.applied,
            L"This fix must not apply optional fill to the larger real caption envelope.");
        Assert::AreEqual(admitted.presentation.sourceBounds.top,preserved.sourceBounds.top);
        Assert::AreEqual(admitted.presentation.sourceBounds.bottom,preserved.sourceBounds.bottom);

        auto full=f.CommitInput();full.previous=anchor;full.sourceSequence=387;full.fullRasterAuthority=true;
        Assert::IsFalse(CommitSubmittedNativeScopeAnchor(full).available,
            L"Real full-raster authority continues to retire the Scope anchor.");
        auto stale=episode;stale.sourceGeneration=3;
        stale.previouslyAdmittedCropAvailable=IsCurrentSubmittedNativeScopeAnchor(anchor,3,386,0,3840,2160);
        Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),
            int(EvaluateNearBlackPresentationEpisode(stale).state.mode),
            L"A prior-generation anchor must not suppress a legitimate full-raster fallback.");
    }
};


    TEST_CLASS(MovingPictureDownstreamOwnershipTests)
    {
        static PresentationRecoveryInput Frame(int top = 248, int bottom = 1912)
        {
            // Geometry from logged source993; prior admission and the exact
            // motion certificate are explicit test assumptions, not recovered
            // telemetry. The trace does not log the pre-uncertainty admission.
            PresentationRecoveryInput r;
            r.crop = TrustedScopeCrop();
            auto& c = r.crop;
            c.geometry = {0,top,3840,bottom,3840,2160,
                3840.0/(bottom-top),ActivePictureBounds::BarAxes::TOP_BOTTOM};
            c.frameSourceGeneration = c.geometrySourceGeneration = 2;
            c.frameSourceSequence = 993;
            c.framePresentationEpoch = r.presentationEpoch = 0;
            c.latestObservationSupportsCrop = false;
            c.latestObservationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            c.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
            c.movingPictureTransition = true;
            c.movingPictureHold.base = c.geometry;
            c.movingPictureHold.sourceGeneration = 2;
            c.movingPictureHold.sourceSequence = 993;
            c.movingPictureHold.presentationEpoch = 0;
            c.currentVisibleBoundsAvailable = true;
            c.currentVisibleBase = c.geometry;
            c.currentVisibleSourceGeneration = 2;
            c.currentVisibleSourceSequence = 993;
            c.currentVisibleBounds = {0,176,3840,2046,3840,2160,0,
                ActivePictureBounds::BarAxes::NONE};
            r.previousAdmission.available = true;
            r.previousAdmission.trustedCrop = c.geometry;
            r.previousAdmission.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
            r.previousAdmission.sourceGeneration = 2;
            r.previousAdmission.presentationEpoch = 0;
            r.previousAdmission.outwardPresentationAvailable = true;
            r.previousAdmission.outwardPresentation = c.currentVisibleBounds;
            r.previousAdmission.presentationSourceSequence = 992;
            r.submittedScopeAnchor.available = true;
            r.submittedScopeAnchor.crop = c.geometry;
            r.submittedScopeAnchor.sourceGeneration = 2;
            r.submittedScopeAnchor.presentationEpoch = 0;
            r.submittedScopeAnchor.lastSubmittedSourceSequence = 960;
            r.submittedScopeAnchor.lastSubmittedPresentationSourceSequence = 992;
            r.submittedScopeAnchor.submittedEnvelopeAvailable = true;
            r.submittedScopeAnchor.submittedEnvelope = c.currentVisibleBounds;
            r.submittedScopeAnchor.submittedEnvelopeSourceSequence = 992;
            r.measurementCurrent = r.retentionEvaluated = r.observationAvailable = true;
            r.retentionSourceGeneration = 2;
            r.retentionSourceSequence = 993;
            r.retentionBounds = c.geometry;
            r.observation = r.observedTrustedCrop = {0,190,3840,1970,3840,2160,0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            r.observationClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
            r.nearBlackEvaluated = true;
            r.framesPerSecond = 24;
            return r;
        }
        struct Selected
        {
            Decision evaluated;
            PresentationRecoveryDecision recovery;
            CropPresentationAdmissionDecision admission;
            UncertainPicturePresentationDecision uncertainty;
            AspectLimitFillDecision fill;
        };
        static Selected Select(PresentationRecoveryInput r)
        {
            Selected s;
            r.candidate = s.evaluated = Evaluate(r.crop);
            s.recovery = EvaluatePresentationRecovery(r);
            r.candidate = s.recovery.presentation;
            const auto bounded = EvaluateNearBlackBoundedPresentation({},r);
            s.admission = AdmitCropPresentation(r.previousAdmission,r.crop,
                bounded.presentation,r.presentationEpoch);
            s.uncertainty = ResolveUncertainPicturePresentation(r,s.admission.presentation);
            FinalAspectLimitFillInput f;
            f.crop = r.crop;
            f.evidence = r;
            f.presentation = s.uncertainty.presentation;
            f.uncertainPicture = s.uncertainty;
            f.recoveryBoundedPresentation = s.recovery.boundedPresentation;
            f.configuredScreenActive = true;
            f.configured.screenAspect = 2.35;
            f.configured.cropNarrowerContentToFillScreen = true;
            f.configured.narrowerLimitConfigured = true;
            f.configured.narrowerAspectLimit = 2.32;
            f.configured.cropWiderContentToFillScreen = true;
            f.configured.widerLimitConfigured = true;
            f.configured.widerAspectLimit = 2.42;
            s.fill = EvaluateFinalAspectLimitFill(f);
            return s;
        }
    public:
        TEST_METHOD(CurrentAdmittedMovingHoldSurvivesEntireDownstreamChainAndFill)
        {
            for (const bool originalScope : {false,true})
            {
                const auto r = originalScope ? Frame(274,1886) : Frame();
                const auto s = Select(r);
                Assert::IsTrue(s.evaluated.owner == DecisionOwner::MOVING_PICTURE_HOLD);
                Assert::IsFalse(s.recovery.state.active || s.admission.blocked || s.uncertainty.retained);
                Assert::IsTrue(s.admission.presentation.owner == DecisionOwner::MOVING_PICTURE_HOLD);
                Assert::IsTrue(s.uncertainty.presentation.owner == DecisionOwner::MOVING_PICTURE_HOLD);
                Assert::AreEqual(r.crop.geometry.top,s.fill.sourceBounds.top);
                Assert::AreEqual(r.crop.geometry.bottom,s.fill.sourceBounds.bottom);
                Assert::AreEqual(originalScope,s.fill.applied);
                Assert::AreEqual(r.crop.geometry.top,s.admission.state.trustedCrop.top);
                Assert::AreEqual(r.crop.geometry.bottom,s.admission.state.trustedCrop.bottom);
            }
        }
        TEST_METHOD(CompetingOrUnadmittedMovingCandidateCannotBypassVisibleEnvelope)
        {
            for (int failure=0;failure<7;++failure)
            {
                auto r = Frame();
                if (failure==0) r.crop.movingPictureHold.competingPresentation = true;
                if (failure==1) r.crop.barCropRefinementHorizontalConflict = true;
                if (failure==2) r.crop.verticalFitConfirmationPending = true;
                if (failure==3) --r.crop.movingPictureHold.sourceSequence;
                if (failure==4) r.previousAdmission = {};
                if (failure==5) ++r.previousAdmission.presentationEpoch;
                if (failure==6) r.previousAdmission.trustedCrop.top -= 2;
                const auto s = Select(r);
                if (failure>=4) Assert::IsTrue(s.admission.blocked);
                else Assert::IsFalse(s.evaluated.applyCrop);
                Assert::IsTrue(s.uncertainty.retained && s.uncertainty.expanded);
                Assert::IsTrue(s.uncertainty.presentation.owner == DecisionOwner::OUTWARD_FIT);
                Assert::AreEqual(176,s.fill.sourceBounds.top);
                Assert::AreEqual(2046,s.fill.sourceBounds.bottom);
                Assert::IsFalse(s.fill.applied);
            }
        }
        TEST_METHOD(LoggedLogicalBaseChangeExplainsUnadmittedMovingHoldFallback)
        {
            // Sources960/1008 retain admission/anchor274..1886 while the
            // native model and motion base already changed to248..1912.
            // The later recovery log's trusted rectangle is the native model;
            // it is not proof that the same crop reached admission.
            auto r = Frame();
            const auto submitted = Frame(274,1886).crop.geometry;
            r.previousAdmission.trustedCrop = submitted;
            r.submittedScopeAnchor.crop = submitted;
            const auto changed = Select(r);
            Assert::IsTrue(changed.evaluated.owner == DecisionOwner::MOVING_PICTURE_HOLD);
            Assert::IsTrue(changed.admission.blocked);
            Assert::IsTrue(changed.uncertainty.retained && changed.uncertainty.expanded);
            Assert::IsTrue(changed.uncertainty.presentation.owner == DecisionOwner::OUTWARD_FIT);
            Assert::AreEqual(274,changed.uncertainty.nativeCrop.top);
            Assert::AreEqual(1886,changed.uncertainty.nativeCrop.bottom);
            Assert::AreEqual(176,changed.fill.sourceBounds.top);
            Assert::AreEqual(2046,changed.fill.sourceBounds.bottom);

            // When onset deferral preserves the actual admitted logical base,
            // the same current visible extent cannot steal the valid hold.
            r.crop.geometry = r.crop.movingPictureHold.base = submitted;
            r.crop.currentVisibleBase = r.retentionBounds = submitted;
            const auto deferred = Select(r);
            Assert::IsFalse(deferred.admission.blocked || deferred.uncertainty.retained);
            Assert::IsTrue(deferred.uncertainty.presentation.owner == DecisionOwner::MOVING_PICTURE_HOLD);
            Assert::AreEqual(274,deferred.fill.sourceBounds.top);
            Assert::AreEqual(1886,deferred.fill.sourceBounds.bottom);
            Assert::IsTrue(deferred.fill.applied);
        }
        TEST_METHOD(FullRasterAuthorityStillReleasesMovingHoldImmediately)
        {
            auto r = Frame(274,1886);
            r.crop.fullRasterPresentationAuthoritative = true;
            const auto s = Select(r);
            Assert::IsFalse(s.evaluated.applyCrop || s.uncertainty.retained || s.admission.state.available);
            Assert::AreEqual(0,s.fill.sourceBounds.top);
            Assert::AreEqual(2160,s.fill.sourceBounds.bottom);
        }
    };

    TEST_CLASS(RefinementPresentationProvenanceTests)
    {
        struct Selected
        {
            Decision evaluated;
            CropPresentationAdmissionDecision admitted;
            UncertainPicturePresentationDecision uncertainty;
            ActivePictureBounds actual;
        };
        static PresentationRecoveryInput Frame(uint64_t sequence=5561, bool refinement=true)
        {
            PresentationRecoveryInput r;
            r.crop=TrustedScopeCrop();
            auto& c=r.crop;
            c.geometry={0,388,3840,1772,3840,2160,3840.0/1384,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            c.frameSourceSequence=sequence;c.framePresentationEpoch=9;
            c.latestObservationSupportsCrop=false;
            c.latestObservationClassification=refinement?ActivePictureClassification::BAR_CROP_TRUSTED:
                ActivePictureClassification::PROVISIONAL;
            c.latestObservationIsProvisional=!refinement;
            c.barCropRefinementPending=refinement;
            c.frameLocalPresentationRetentionEvaluated=true;
            c.outwardPresentationActive=c.outwardExpansionAvailable=true;
            c.outwardExpansion=c.geometry;c.outwardExpansion.top=376;c.outwardExpansion.bottom=2056;
            c.outwardExpansion.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            c.outwardExpansionSourceGeneration=c.frameSourceGeneration;
            c.currentVisibleBoundsAvailable=true;c.currentVisibleBase=c.geometry;
            c.currentVisibleBounds=c.outwardExpansion;
            c.currentVisibleSourceGeneration=c.frameSourceGeneration;c.currentVisibleSourceSequence=sequence;
            r.presentationEpoch=9;r.measurementCurrent=r.retentionEvaluated=r.observationAvailable=true;
            r.retentionBounds=c.geometry;r.retentionSourceGeneration=c.frameSourceGeneration;
            r.retentionSourceSequence=sequence;r.observationClassification=c.latestObservationClassification;
            r.observation=refinement?ActivePictureBounds{0,376,3840,1784,3840,2160,0.0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM}:ActivePictureBounds{0,534,3840,2010,3840,2160,0.0,
                ActivePictureBounds::BarAxes::NONE};
            r.previousAdmission.available=true;r.previousAdmission.trustedCrop=c.geometry;
            r.previousAdmission.sourceGeneration=c.frameSourceGeneration;r.previousAdmission.presentationEpoch=9;
            r.submittedScopeAnchor.available=true;r.submittedScopeAnchor.crop=c.geometry;
            r.submittedScopeAnchor.sourceGeneration=c.frameSourceGeneration;r.submittedScopeAnchor.presentationEpoch=9;
            r.submittedScopeAnchor.lastSubmittedSourceSequence=5550;
            r.submittedScopeAnchor.lastSubmittedPresentationSourceSequence=5560;
            r.submittedScopeAnchor.submittedEnvelopeAvailable=true;
            r.submittedScopeAnchor.submittedEnvelope=c.geometry;
            r.submittedScopeAnchor.submittedEnvelope.bottom=1776;
            r.submittedScopeAnchor.submittedEnvelopeSourceSequence=5560;
            r.candidate=Evaluate(c);
            return r;
        }
        static Selected Select(PresentationRecoveryInput& r,bool composed)
        {
            Selected s;
            s.evaluated=r.candidate=Evaluate(r.crop);
            const auto recovery=EvaluatePresentationRecovery(r);
            s.admitted=AdmitCropPresentation(r.previousAdmission,r.crop,recovery.presentation,r.presentationEpoch);
            s.uncertainty=ResolveUncertainPicturePresentation(r,s.admitted.presentation);
            s.actual=SubtitlePresentationBounds(s.uncertainty.presentation,
                SubtitlePreviewMode::GeneratedGrayBackground,composed,false);
            return s;
        }
        static SubmittedNativeScopeAnchorUpdateInput Submission(const PresentationRecoveryInput& r,const Selected& s)
        {
            SubmittedNativeScopeAnchorUpdateInput u;
            u.previous=r.submittedScopeAnchor;u.selectedAdmission=s.admitted.state;
            u.nativeCrop=r.crop.geometry;u.nativeCropAvailable=true;u.submittedBounds=s.actual;
            u.selectedOwner=s.uncertainty.presentation.owner;u.frameSubmitted=true;
            u.sourceGeneration=r.crop.frameSourceGeneration;u.sourceSequence=r.crop.frameSourceSequence;
            u.presentationEpoch=r.presentationEpoch;return u;
        }
        static void Qualify(PresentationRecoveryInput& r)
        {
            QualifyExplicitCaptionFitFixture(r,{1311,1930,2518,2028,3840,2160,0.0,
                ActivePictureBounds::BarAxes::NONE});
            r.confirmedPresentationResolved=true;
        }
    public:
        TEST_METHOD(UnqualifiedRefinementCannotBypassTheUncertainFitPolicy)
        {
            for(bool composed:{false,true})for(bool refinement:{false,true}) {
                auto r=Frame(5561,refinement);const auto s=Select(r,composed);
                Assert::IsTrue(s.evaluated.outwardExpanded);
                Assert::IsTrue(s.evaluated.owner==(refinement?DecisionOwner::BAR_REFINEMENT:DecisionOwner::OUTWARD_FIT));
                Assert::IsTrue(s.uncertainty.retained,
                    L"A native refinement label cannot independently authorize a new vertical presentation.");
                Assert::AreEqual(388,s.actual.top);Assert::AreEqual(1776,s.actual.bottom);
            }
        }
        TEST_METHOD(QualifiedRefinementSubmissionSurvivesFollowingUncertainFrames)
        {
            for(bool composed:{false,true}) {
                auto r=Frame();Qualify(r);const auto chosen=Select(r,composed);
                Assert::IsTrue(chosen.uncertainty.presentation.owner==DecisionOwner::BAR_REFINEMENT);
                Assert::IsFalse(chosen.uncertainty.retained);
                Assert::AreEqual(376,chosen.actual.top);Assert::AreEqual(2056,chosen.actual.bottom);
                const auto submitted=CommitSubmittedNativeScopeAnchor(Submission(r,chosen));
                Assert::AreEqual(2056,submitted.submittedEnvelope.bottom,
                    L"Record independently qualified actual output, separately from the native refinement owner.");
                Assert::AreEqual(uint64_t(5550),submitted.lastSubmittedSourceSequence);
                Assert::AreEqual(388,submitted.crop.top);Assert::AreEqual(1772,submitted.crop.bottom);
                for(uint64_t sequence:{5562ull,5563ull,5565ull,5566ull}) {
                    auto next=Frame(sequence,sequence==5565);next.submittedScopeAnchor=submitted;
                    const auto held=Select(next,!composed);
                    Assert::AreEqual(376,held.actual.top);Assert::AreEqual(2056,held.actual.bottom);
                }
            }
        }
        TEST_METHOD(FailedSubmissionAndReplayedSourceCannotInventOrRenewEnvelope)
        {
            auto r=Frame();Qualify(r);const auto chosen=Select(r,true);
            auto u=Submission(r,chosen);u.frameSubmitted=false;
            auto anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::AreEqual(1776,anchor.submittedEnvelope.bottom);
            Assert::AreEqual(uint64_t(5560),anchor.lastSubmittedPresentationSourceSequence);
            u.frameSubmitted=true;anchor=CommitSubmittedNativeScopeAnchor(u);
            u.previous=anchor;
            const auto repeated=CommitSubmittedNativeScopeAnchor(u);
            Assert::AreEqual(anchor.submittedEnvelopeSourceSequence,repeated.submittedEnvelopeSourceSequence);
            Assert::AreEqual(anchor.lastSubmittedSourceSequence,repeated.lastSubmittedSourceSequence);
            u.sourceSequence=5560;
            const auto late=CommitSubmittedNativeScopeAnchor(u);
            Assert::AreEqual(uint64_t(5561),late.submittedEnvelopeSourceSequence);
            auto unknown=Frame(5566,false);unknown.submittedScopeAnchor=anchor;
            Assert::AreEqual(2056,Select(unknown,false).actual.bottom);
        }
        TEST_METHOD(LateQualifiedCaptionProofMustTravelWithFinalSelectedAdmission)
        {
            auto r=Frame();
            r.candidate=Evaluate(r.crop);
            const auto recovery=EvaluatePresentationRecovery(r);
            auto admission=AdmitCropPresentation(r.previousAdmission,r.crop,recovery.presentation,r.presentationEpoch);
            Assert::IsFalse(admission.state.confirmedFitOwnership.available);
            // The renderer can discover exact caption ownership after ordinary
            // admission. It must copy that final presentation-only certificate
            // into the selected packet without acquiring a new native contract.
            Qualify(r);
            const auto final=ResolveUncertainPicturePresentation(r,admission.presentation);
            Assert::IsFalse(final.retained);
            Assert::IsTrue(final.presentation.confirmedFitOwnership.available);
            Selected s;s.admitted=admission;s.uncertainty=final;s.actual=final.presentation.sourceBounds;
            Assert::AreEqual(1776,CommitSubmittedNativeScopeAnchor(Submission(r,s)).submittedEnvelope.bottom,
                L"An unrelated proposal cannot substitute for the selected packet certificate.");
            s.admitted.presentation.confirmedFitOwnership=final.presentation.confirmedFitOwnership;
            s.admitted.state.confirmedFitOwnership=final.presentation.confirmedFitOwnership;
            const auto submitted=CommitSubmittedNativeScopeAnchor(Submission(r,s));
            Assert::AreEqual(2056,submitted.submittedEnvelope.bottom);
            Assert::AreEqual(uint64_t(5550),submitted.lastSubmittedSourceSequence);
        }

        TEST_METHOD(RefinementFitProofMustMatchCurrentFrameAndExactPresentation)
        {
            for(int fault=0;fault<9;++fault) {
                auto r=Frame();Qualify(r);
                auto& proof=r.confirmedFitOwnership;
                if(fault==0)proof.available=false;
                if(fault==1)--proof.sourceSequence;
                if(fault==2)++proof.sourceGeneration;
                if(fault==3)++proof.presentationEpoch;
                if(fault==4)proof.nativeBase.top+=2;
                if(fault==5)proof.approvedBounds.bottom-=2;
                if(fault==6)proof.origin=ConfirmedFitOwnershipOrigin::NONE;
                if(fault==7)++r.crop.framePresentationEpoch;
                if(fault==8)proof.approvedBounds.rasterHeight+=2;
                const auto chosen=Select(r,true);
                Assert::IsTrue(chosen.uncertainty.retained);
                Assert::IsFalse(chosen.admitted.state.confirmedFitOwnership.available);
                Assert::AreEqual(388,chosen.actual.top);Assert::AreEqual(1776,chosen.actual.bottom);
            }
        }
        TEST_METHOD(SubmissionRechecksSelectedProofAndActualBounds)
        {
            auto r=Frame();Qualify(r);const auto chosen=Select(r,true);
            Assert::IsTrue(chosen.admitted.state.confirmedFitOwnership.available);
            for(int fault=0;fault<11;++fault) {
                auto u=Submission(r,chosen);auto& proof=u.selectedAdmission.confirmedFitOwnership;
                if(fault==0)proof.available=false;
                if(fault==1)--proof.sourceSequence;
                if(fault==2)++proof.sourceGeneration;
                if(fault==3)++proof.presentationEpoch;
                if(fault==4)proof.nativeBase.bottom-=2;
                if(fault==5)proof.approvedBounds.bottom-=2;
                if(fault==6)u.submittedBounds.bottom-=2;
                if(fault==7)u.selectedAdmission.available=false;
                if(fault==8)++u.selectedAdmission.presentationEpoch;
                if(fault==9)u.selectedAdmission.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
                if(fault==10)proof.approvedBounds.rasterWidth+=2;
                const char* reason=nullptr;
                const auto rejected=CommitSubmittedNativeScopeAnchor(u,&reason);
                Assert::AreEqual(1776,rejected.submittedEnvelope.bottom);
                Assert::AreEqual(uint64_t(5560),rejected.submittedEnvelopeSourceSequence);
                Assert::IsTrue(reason && std::string(reason)=="actual-geometry-not-qualified");
            }
        }
        TEST_METHOD(ProofCannotBootstrapNativeAuthorityOrCrossPresentationEpoch)
        {
            auto r=Frame();Qualify(r);const auto chosen=Select(r,false);
            for(int fault=0;fault<3;++fault) {
                auto u=Submission(r,chosen);
                if(fault==0)u.previous={};
                if(fault==1)++u.presentationEpoch;
                if(fault==2)++u.sourceGeneration;
                const auto rejected=CommitSubmittedNativeScopeAnchor(u);
                Assert::IsFalse(rejected.available || rejected.submittedEnvelopeAvailable);
            }
            auto u=Submission(r,chosen);const char* reason=nullptr;
            u.frameSubmitted=false;
            CommitSubmittedNativeScopeAnchor(u,&reason);
            Assert::IsTrue(reason && std::string(reason)=="not-submitted");
            u.frameSubmitted=true;
            const auto actual=CommitSubmittedNativeScopeAnchor(u,&reason);
            Assert::IsTrue(reason && std::string(reason)=="qualified-fit-envelope-committed");
            u.previous=actual;--u.sourceSequence;
            CommitSubmittedNativeScopeAnchor(u,&reason);
            Assert::IsTrue(reason && std::string(reason)=="nonmonotonic-source");
            u.sourceSequence=5562;u.fullRasterAuthority=true;
            const auto full=CommitSubmittedNativeScopeAnchor(u,&reason);
            Assert::IsFalse(full.available);
            Assert::IsTrue(reason && std::string(reason)=="full-authority-cleared");
        }

        TEST_METHOD(IndependentPhysicalExpansionAndFullAuthorityKeepTheirOwnContract)
        {
            auto r=Frame();r.crop.geometry.top=360;r.crop.geometry.bottom=1800;
            r.crop.latestObservationSupportsCrop=true;r.crop.barCropRefinementPending=false;
            r.crop.outwardPresentationActive=r.crop.outwardExpansionAvailable=false;
            r.crop.currentVisibleBoundsAvailable=false;r.retentionBounds=r.crop.geometry;
            const auto expanded=Select(r,false);
            Assert::IsFalse(expanded.uncertainty.retained);
            Assert::AreEqual(360,expanded.actual.top);Assert::AreEqual(1800,expanded.actual.bottom);
            const auto native=CommitSubmittedNativeScopeAnchor(Submission(r,expanded));
            Assert::AreEqual(1800,native.crop.bottom);
            r.crop.fullRasterPresentationAuthoritative=true;
            const auto full=Select(r,true);
            Assert::IsFalse(full.uncertainty.retained);
            Assert::AreEqual(0,full.actual.top);Assert::AreEqual(2160,full.actual.bottom);
        }
    };

    TEST_CLASS(AcceptedNativePresentationContinuityTests)
    {
        static Input Native(uint64_t sequence=10707)
        {
            auto c=TrustedScopeCrop();
            c.geometry={0,256,3840,1904,3840,2160,3840.0/1648,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            c.frameSourceSequence=sequence;c.framePresentationEpoch=9;
            c.frameSourceFormatKey=11;c.frameAnalysisDomainKey=12;
            c.acceptedNativeCrop.available=true;c.acceptedNativeCrop.bounds=c.geometry;
            c.acceptedNativeCrop.sourceGeneration=c.frameSourceGeneration;
            c.acceptedNativeCrop.acceptedSourceSequence=10700;
            c.acceptedNativeCrop.sourceFormatKey=11;c.acceptedNativeCrop.analysisDomainKey=12;
            return c;
        }
        static SubmittedNativeScopeAnchorUpdateInput Submission(Input c)
        {
            SubmittedNativeScopeAnchorUpdateInput u;
            u.nativeCropAvailable=u.frameSubmitted=true;u.nativeCrop=c.geometry;
            u.submittedBounds=c.geometry;u.sourceGeneration=c.frameSourceGeneration;
            u.sourceSequence=c.frameSourceSequence;u.presentationEpoch=c.framePresentationEpoch;
            u.selectedOwner=DecisionOwner::TRUSTED_CROP;
            u.selectedAdmission=AdmitCropPresentation({},c,Evaluate(c),u.presentationEpoch).state;
            return u;
        }
        static void AuthorizeActual(SubmittedNativeScopeAnchorUpdateInput& u,
            NativePresentationOrigin origin)
        {
            auto& p=u.selectedAdmission.nativePresentation;
            p.available=true;p.origin=origin;p.nativeBase=u.nativeCrop;p.approvedBounds=u.submittedBounds;
            p.sourceGeneration=u.sourceGeneration;p.sourceSequence=u.sourceSequence;p.presentationEpoch=u.presentationEpoch;
        }
        static PresentationRecoveryInput Unknown(Input c, const SubmittedNativeScopeAnchor& anchor)
        {
            c.latestObservationSupportsCrop=false;c.latestObservationIsProvisional=true;
            c.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            c.frameLocalPresentationRetentionEvaluated=true;c.frameLocalPresentationRetentionSafe=false;
            PresentationRecoveryInput r;r.crop=c;r.submittedScopeAnchor=anchor;
            r.presentationEpoch=c.framePresentationEpoch;r.candidate=Evaluate(c);
            r.measurementCurrent=r.retentionEvaluated=r.excludedBandsPixelSafe=r.observationAvailable=true;
            r.retentionBounds=c.geometry;r.retentionSourceGeneration=c.frameSourceGeneration;
            r.retentionSourceSequence=c.frameSourceSequence;
            r.observation={4,254,3840,1766,3840,2160,3836.0/1512,ActivePictureBounds::BarAxes::NONE};
            r.observationClassification=ActivePictureClassification::PROVISIONAL;
            return r;
        }
    public:
        TEST_METHOD(AuditPrimeRefinedSubmissionMustSurviveProvisionalObservation)
        {
            auto c=Native();auto u=Submission(c);
            // Prime's 262..1896 is an authorized configured fill, not a second
            // native detection. Recompute that output before certifying it.
            AspectLimitFillInput fill;fill.sourceBounds=c.geometry;fill.screenAspect=2.35;
            fill.trustedContentAuthorityAccepted=true;
            fill.cropNarrowerContentToFillScreen=fill.narrowerLimitConfigured=true;
            fill.narrowerAspectLimit=2.32;
            const auto actual=EvaluateAspectLimitFill(fill);Assert::IsTrue(actual.applied);
            u.submittedBounds=actual.sourceBounds;u.selectedOwner=DecisionOwner::BAR_REFINEMENT;
            AuthorizeActual(u,NativePresentationOrigin::CONFIGURED_FILL);
            const auto anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(anchor.available && anchor.submittedPresentationAvailable);
            Assert::AreEqual(256,anchor.crop.top);Assert::AreEqual(1904,anchor.crop.bottom);
            for(uint64_t seq=11002;seq<11022;++seq) {
                auto r=Unknown(Native(seq),anchor);r.previousAdmission=u.selectedAdmission;
                const auto recovery=EvaluatePresentationRecovery(r);
                const auto admission=AdmitCropPresentation(r.previousAdmission,r.crop,recovery.presentation,9);
                const auto final=ResolveUncertainPicturePresentation(r,admission.presentation);
                Assert::IsTrue(final.presentation.applyCrop);
                Assert::AreEqual(actual.sourceBounds.top,final.presentation.sourceBounds.top);
                Assert::AreEqual(actual.sourceBounds.bottom,final.presentation.sourceBounds.bottom);
                Assert::AreEqual(256,final.nativeCrop.top);
            }
        }
        TEST_METHOD(FirstConfirmedFitCommitsNativeAndActualIndependently)
        {
            auto u=Submission(Native());u.selectedOwner=DecisionOwner::OUTWARD_FIT;
            u.submittedBounds.top=120;u.submittedBounds.bottom=1948;
            AuthorizeActual(u,NativePresentationOrigin::CONFIRMED_FIT);
            const auto anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(anchor.available && anchor.submittedEnvelopeAvailable);
            Assert::AreEqual(256,anchor.crop.top);Assert::AreEqual(120,anchor.submittedPresentation.top);
            auto r=Unknown(Native(11002),anchor);
            const auto held=ResolveUncertainPicturePresentation(r,Evaluate(r.crop));
            Assert::AreEqual(120,held.presentation.sourceBounds.top);
            Assert::AreEqual(1948,held.presentation.sourceBounds.bottom);
        }
        TEST_METHOD(FirstTransformedSubmissionRequiresBothExactCertificates)
        {
            for(int fault=0;fault<11;++fault) {
                auto u=Submission(Native());u.selectedOwner=DecisionOwner::OUTWARD_FIT;
                u.submittedBounds.top=120;AuthorizeActual(u,NativePresentationOrigin::CONFIRMED_FIT);
                if(fault==0)u.selectedAdmission.acceptedNativeCrop.available=false;
                if(fault==1)u.selectedAdmission.acceptedNativeCrop.bounds.top+=2;
                if(fault==2)u.selectedAdmission.acceptedNativeCrop.sourceGeneration++;
                if(fault==3)u.selectedAdmission.acceptedNativeCrop.acceptedSourceSequence=u.sourceSequence+1;
                if(fault==4)u.selectedAdmission.nativePresentation.available=false;
                if(fault==5)u.selectedAdmission.nativePresentation.nativeBase.top+=2;
                if(fault==6)u.selectedAdmission.nativePresentation.approvedBounds.top+=2;
                if(fault==7)u.selectedAdmission.nativePresentation.sourceSequence--;
                if(fault==8)u.selectedAdmission.nativePresentation.presentationEpoch++;
                if(fault==9)u.frameSubmitted=false;
                if(fault==10)u.selectedAdmission.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
                Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available);
            }
        }
        TEST_METHOD(QualifiedUnknownHoldsNativeButSourceAndFormatChangesDoNot)
        {
            auto r=Unknown(Native(11002),{});
            Assert::IsTrue(Evaluate(r.crop).applyCrop);
            r.crop.latestObservationIsProvisional=false;r.crop.latestObservationIsUnavailable=true;
            Assert::IsTrue(Evaluate(r.crop).applyCrop);
            r.crop.frameSourceGeneration++;
            Assert::IsFalse(Evaluate(r.crop).applyCrop);
            r=Unknown(Native(11002),{});r.crop.frameSourceFormatKey++;
            Assert::IsFalse(Evaluate(r.crop).applyCrop);
            r=Unknown(Native(11002),{});r.crop.fullRasterPresentationAuthoritative=true;
            Assert::IsFalse(ResolveUncertainPicturePresentation(r,Evaluate(r.crop)).presentation.applyCrop);
        }
        TEST_METHOD(ConfirmedNativeExpansionNeedsNoCaptionOwnerOrRecoveryDwell)
        {
            auto r=Unknown(Native(11002),{});
            auto& p=r.nativeOutwardPictureProof;
            p.base=r.crop.geometry;p.candidate=p.referenceCandidate=r.crop.geometry;
            p.candidate.top=p.referenceCandidate.top=68;p.candidate.bottom=p.referenceCandidate.bottom=2092;
            p.sourceGeneration=r.crop.frameSourceGeneration;p.lastObservedSourceSequence=11002;
            p.firstPictureSourceSequence=11000;p.confirmations=OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED;
            p.proofRoute=OutwardPictureProofRoute::BROAD_OCCUPANCY;
            r.nativeOutwardPictureProofPresentationEpoch=9;
            for(int fault=-1;fault<5;++fault) {
                auto f=r;
                if(fault==0)f.nativeOutwardPictureProof.lastObservedSourceSequence--;
                if(fault==1)f.nativeOutwardPictureProof.sourceGeneration++;
                if(fault==2)f.nativeOutwardPictureProofPresentationEpoch++;
                if(fault==3)f.nativeOutwardPictureProof.confirmations=0;
                if(fault==4)f.nativeOutwardPictureProof.proofRoute=OutwardPictureProofRoute::NONE;
                const auto recovery=EvaluatePresentationRecovery(f);
                const auto admission=AdmitCropPresentation({},f.crop,recovery.presentation,9);
                const auto final=ResolveUncertainPicturePresentation(f,admission.presentation);
                Assert::AreEqual(fault<0?68:256,final.presentation.sourceBounds.top);
                Assert::AreEqual(fault<0?2092:1904,final.presentation.sourceBounds.bottom);
                Assert::AreEqual(256,f.crop.geometry.top);
            }
            r.nativeOutwardPictureProof.confirmations=1;
            const auto prompt=EvaluatePresentationRecovery(r);
            Assert::AreEqual(68,prompt.presentation.sourceBounds.top,
                L"A qualified spatial witness preserves picture before three-vote native AR publication.");
        }
        TEST_METHOD(FirstRefinementRequiresIndependentExactPresentationNotAnOwnerLabel)
        {
            auto c=Native();auto proposal=Evaluate(c);proposal.owner=DecisionOwner::BAR_REFINEMENT;
            const auto exact=AdmitCropPresentation({},c,proposal,9);
            Assert::IsFalse(exact.blocked);
            auto u=Submission(c);u.selectedAdmission=exact.state;u.selectedOwner=proposal.owner;
            const auto native=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(native.available);
            Assert::AreEqual(256,native.crop.top);Assert::AreEqual(256,native.submittedPresentation.top);
            proposal.sourceBounds.top=262;proposal.sourceBounds.bottom=1896;
            auto noProof=AdmitCropPresentation({},c,proposal,9);
            Assert::IsTrue(noProof.blocked);
            u.submittedBounds=proposal.sourceBounds;
            Assert::IsFalse(CommitSubmittedNativeScopeAnchor(u).available,
                L"BAR_REFINEMENT cannot authorize a different actual rectangle by label alone.");
        }
        TEST_METHOD(NativeExpansionAndPublicationPreserveOnlyVerifiedSubmittedCaptionExtent)
        {
            auto c=Native();auto u=Submission(c);
            Decision fit;fit.applyCrop=fit.outwardExpanded=true;fit.owner=DecisionOwner::OUTWARD_FIT;
            fit.sourceBounds=c.geometry;fit.sourceBounds.bottom=2140;
            ConfirmedFitOwnershipInput proof;proof.origin=ConfirmedFitOwnershipOrigin::CURRENT_CAPTION;
            proof.current=proof.confirmed=true;proof.nativeBase=c.geometry;proof.approvedBounds=fit.sourceBounds;
            proof.evidenceBounds={1300,2050,2500,2138,3840,2160,0.0,ActivePictureBounds::BarAxes::NONE};
            proof.sourceGeneration=c.frameSourceGeneration;proof.sourceSequence=c.frameSourceSequence;proof.presentationEpoch=9;
            fit.confirmedFitOwnership=QualifyConfirmedFitOwnership(proof);
            Assert::IsTrue(fit.confirmedFitOwnership.available);
            const auto admittedFit=AdmitCropPresentation({},c,fit,9);
            Assert::IsFalse(admittedFit.blocked);
            u.selectedAdmission=admittedFit.state;u.selectedOwner=fit.owner;u.submittedBounds=fit.sourceBounds;
            auto anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(anchor.captionPreservation.available);
            Assert::AreEqual(1300,anchor.captionPreservation.extent.left);
            Assert::AreEqual(2140,anchor.captionPreservation.extent.bottom);
            const auto select=[&](PresentationRecoveryInput& r) {
                const auto recovery=EvaluatePresentationRecovery(r);
                auto admission=AdmitCropPresentation(r.previousAdmission,r.crop,recovery.presentation,r.presentationEpoch);
                const auto final=ResolveUncertainPicturePresentation(r,admission.presentation);
                if(final.retained) {
                    admission.state.available=true;admission.state.trustedCrop=final.nativeCrop;
                    admission.state.sourceGeneration=r.crop.frameSourceGeneration;admission.state.presentationEpoch=r.presentationEpoch;
                }
                if(final.presentation.nativePresentation.available)
                    admission.state.nativePresentation=final.presentation.nativePresentation;
                admission.state.confirmedFitOwnership=final.presentation.confirmedFitOwnership;
                u.previous=r.submittedScopeAnchor;u.selectedAdmission=admission.state;u.nativeCrop=r.crop.geometry;
                u.sourceGeneration=r.crop.frameSourceGeneration;u.sourceSequence=r.crop.frameSourceSequence;
                u.presentationEpoch=r.presentationEpoch;u.submittedBounds=final.presentation.sourceBounds;
                u.selectedOwner=final.presentation.owner;
                return final;
            };
            auto r=Unknown(Native(11002),anchor);
            auto& outward=r.nativeOutwardPictureProof;
            outward.base=r.crop.geometry;outward.candidate=outward.referenceCandidate=r.crop.geometry;
            outward.candidate.top=outward.referenceCandidate.top=68;
            outward.candidate.bottom=outward.referenceCandidate.bottom=2092;
            outward.sourceGeneration=7;outward.lastObservedSourceSequence=11002;
            outward.firstPictureSourceSequence=11002;outward.confirmations=1;
            outward.proofRoute=OutwardPictureProofRoute::BROAD_OCCUPANCY;r.nativeOutwardPictureProofPresentationEpoch=9;
            auto selected=select(r);
            Assert::AreEqual(68,selected.presentation.sourceBounds.top);
            Assert::AreEqual(2140,selected.presentation.sourceBounds.bottom);
            Assert::AreEqual(256,selected.nativeCrop.top);
            anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(anchor.captionPreservation.available);
            // The model now publishes a genuinely different native AR. The old
            // native rectangle must not be restored to keep the caption visible.
            auto published=Native(11003);published.geometry.top=68;published.geometry.bottom=2092;
            published.acceptedNativeCrop.bounds=published.geometry;published.acceptedNativeCrop.acceptedSourceSequence=11003;
            r=Unknown(published,anchor);r.crop.latestObservationSupportsCrop=true;
            selected=select(r);Assert::AreEqual(68,selected.nativeCrop.top);
            Assert::AreEqual(2092,selected.nativeCrop.bottom);Assert::AreEqual(2140,selected.presentation.sourceBounds.bottom);
            anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::AreEqual(68,anchor.crop.top);Assert::AreEqual(2092,anchor.crop.bottom);
            Assert::AreEqual(uint64_t(10707),anchor.captionPreservation.sourceSequence,
                L"Unknown inspection cannot renew the original caption measurement.");
            published.frameSourceSequence=11004;
            r=Unknown(published,anchor);selected=select(r);
            Assert::AreEqual(2140,selected.presentation.sourceBounds.bottom);
            FinalAspectLimitFillInput configured;configured.crop=r.crop;configured.presentation=selected.presentation;
            configured.uncertainPicture=selected;configured.configuredScreenActive=true;
            configured.configured.sourceBounds=selected.presentation.sourceBounds;configured.configured.screenAspect=2.35;
            configured.configured.trustedContentAuthorityAccepted=true;
            configured.configured.cropNarrowerContentToFillScreen=configured.configured.narrowerLimitConfigured=true;
            configured.configured.narrowerAspectLimit=1.0;
            const auto fill=EvaluateFinalAspectLimitFill(configured);
            Assert::IsFalse(fill.applied);Assert::AreEqual(2140,fill.sourceBounds.bottom);
            const auto status=[&](PresentationRecoveryInput& f,CaptionObservationState state) {
                auto& s=f.captionObservation;s.state=state;s.sourceGeneration=f.crop.frameSourceGeneration;
                s.sourceSequence=f.crop.frameSourceSequence;s.presentationEpoch=f.presentationEpoch;
                s.sourceFormatKey=f.crop.frameSourceFormatKey;s.analysisDomainKey=f.crop.frameAnalysisDomainKey;
            };
            for(int fault=0;fault<7;++fault) {
                auto f=r;status(f,CaptionObservationState::ABSENT);
                if(fault==0)f.captionObservation.sourceSequence--;
                if(fault==1)f.captionObservation.sourceGeneration++;
                if(fault==2)f.captionObservation.presentationEpoch++;
                if(fault==3)f.captionObservation.sourceFormatKey++;
                if(fault==4)f.captionObservation.analysisDomainKey++;
                if(fault==5)f.captionObservation.state=CaptionObservationState::PRESENT;
                const auto final=select(f);
                Assert::AreEqual(fault==6?2092:2140,final.presentation.sourceBounds.bottom);
            }
            for(int mismatch=0;mismatch<4;++mismatch) {
                auto f=r;
                if(mismatch==0){++f.presentationEpoch;f.crop.framePresentationEpoch=f.presentationEpoch;}
                if(mismatch==1){++f.crop.frameSourceFormatKey;f.crop.acceptedNativeCrop.sourceFormatKey=f.crop.frameSourceFormatKey;}
                if(mismatch==2){++f.crop.frameAnalysisDomainKey;f.crop.acceptedNativeCrop.analysisDomainKey=f.crop.frameAnalysisDomainKey;}
                if(mismatch==3){++f.crop.frameSourceGeneration;f.crop.geometrySourceGeneration=f.crop.frameSourceGeneration;
                    f.crop.acceptedNativeCrop.sourceGeneration=f.crop.frameSourceGeneration;}
                Assert::AreEqual(2092,select(f).presentation.sourceBounds.bottom);
            }
            auto replacement=r;status(replacement,CaptionObservationState::PRESENT);
            proof.nativeBase=replacement.crop.geometry;proof.approvedBounds=replacement.crop.geometry;
            proof.approvedBounds.bottom=2120;proof.evidenceBounds.bottom=2118;
            proof.sourceSequence=replacement.crop.frameSourceSequence;
            replacement.confirmedFitOwnership=QualifyConfirmedFitOwnership(proof);
            Assert::IsTrue(replacement.confirmedFitOwnership.available);
            replacement.candidate.sourceBounds=proof.approvedBounds;
            replacement.candidate.applyCrop=replacement.candidate.outwardExpanded=true;
            replacement.candidate.owner=DecisionOwner::OUTWARD_FIT;
            const auto replaced=select(replacement);
            Assert::AreEqual(2120,replaced.presentation.sourceBounds.bottom,
                L"Only qualified current caption replacement may retire the old larger extent.");
            auto absent=r;status(absent,CaptionObservationState::ABSENT);
            const auto clean=select(absent);Assert::AreEqual(2092,clean.presentation.sourceBounds.bottom);
            const auto retired=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(retired.available);Assert::IsFalse(retired.captionPreservation.available);
            Assert::AreEqual(68,retired.crop.top);Assert::AreEqual(2092,retired.crop.bottom);
            // An independently published inward change may remove old picture
            // rows; only the measured caption extent survives, not the old AR.
            auto contracted=r;contracted.crop.geometry.top=300;contracted.crop.geometry.bottom=1800;
            contracted.crop.acceptedNativeCrop.bounds=contracted.crop.geometry;
            contracted.crop.acceptedNativeCrop.acceptedSourceSequence=11004;
            contracted.crop.latestObservationSupportsCrop=true;
            const auto inward=select(contracted);
            Assert::AreEqual(300,inward.nativeCrop.top);Assert::AreEqual(1800,inward.nativeCrop.bottom);
            Assert::AreEqual(300,inward.presentation.sourceBounds.top);Assert::AreEqual(2140,inward.presentation.sourceBounds.bottom);
        }
        static ConfirmedFitOwnershipEvidence CurrentCaption(const Input& c,int bottom)
        {
            ConfirmedFitOwnershipInput q;q.origin=ConfirmedFitOwnershipOrigin::CURRENT_CAPTION;
            q.current=q.confirmed=true;q.nativeBase=q.approvedBounds=c.geometry;q.approvedBounds.bottom=bottom;
            q.evidenceBounds={1300,bottom-90,2500,bottom-2,3840,2160,0.0,ActivePictureBounds::BarAxes::NONE};
            q.sourceGeneration=c.frameSourceGeneration;q.sourceSequence=c.frameSourceSequence;q.presentationEpoch=c.framePresentationEpoch;
            const auto result=QualifyConfirmedFitOwnership(q);Assert::IsTrue(result.available);return result;
        }
        static SubmittedNativeScopeAnchor CaptionAnchor(const Input& c)
        {
            auto u=Submission(c);auto fit=Evaluate(c);
            fit.sourceBounds.bottom=2140;fit.outwardExpanded=true;fit.owner=DecisionOwner::OUTWARD_FIT;
            fit.confirmedFitOwnership=CurrentCaption(c,2140);
            const auto admitted=AdmitCropPresentation({},c,fit,c.framePresentationEpoch);
            Assert::IsFalse(admitted.blocked);u.selectedAdmission=admitted.state;
            u.selectedOwner=fit.owner;u.submittedBounds=fit.sourceBounds;
            const auto anchor=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(anchor.captionPreservation.available);return anchor;
        }
        static Decision StageCaptionSelection(const PresentationRecoveryInput& r,SubmittedNativeScopeAnchorUpdateInput& u)
        {
            const auto recovery=EvaluatePresentationRecovery(r);
            auto admitted=AdmitCropPresentation(r.previousAdmission,r.crop,recovery.presentation,r.presentationEpoch);
            const auto final=ResolveUncertainPicturePresentation(r,admitted.presentation);
            Assert::IsTrue(final.retained || !admitted.blocked);
            if(final.retained) {
                admitted.state.available=true;admitted.state.trustedCrop=final.nativeCrop;
                admitted.state.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
                admitted.state.sourceGeneration=r.crop.frameSourceGeneration;admitted.state.presentationEpoch=r.presentationEpoch;
            }
            if(final.presentation.nativePresentation.available)admitted.state.nativePresentation=final.presentation.nativePresentation;
            admitted.state.confirmedFitOwnership=final.presentation.confirmedFitOwnership;
            u={};u.previous=r.submittedScopeAnchor;u.frameSubmitted=u.nativeCropAvailable=true;
            u.nativeCrop=r.crop.geometry;u.submittedBounds=final.presentation.sourceBounds;u.selectedOwner=final.presentation.owner;
            u.sourceGeneration=r.crop.frameSourceGeneration;u.sourceSequence=r.crop.frameSourceSequence;
            u.presentationEpoch=r.presentationEpoch;u.selectedAdmission=admitted.state;
            return final.presentation;
        }
        TEST_METHOD(SimultaneousNativeExpansionAndNewCaptionPreserveTheirIndependentExtents)
        {
            auto r=Unknown(Native(11002),CaptionAnchor(Native()));
            auto& p=r.nativeOutwardPictureProof;p.base=r.crop.geometry;p.candidate=p.referenceCandidate=r.crop.geometry;
            p.candidate.top=p.referenceCandidate.top=68;p.candidate.bottom=p.referenceCandidate.bottom=2092;
            p.sourceGeneration=r.crop.frameSourceGeneration;p.firstPictureSourceSequence=p.lastObservedSourceSequence=11002;
            p.confirmations=1;p.proofRoute=OutwardPictureProofRoute::BROAD_OCCUPANCY;
            r.nativeOutwardPictureProofPresentationEpoch=9;
            r.confirmedFitOwnership=CurrentCaption(r.crop,2120);
            SubmittedNativeScopeAnchorUpdateInput u;
            const auto selected=StageCaptionSelection(r,u);
            Assert::AreEqual(68,selected.sourceBounds.top);Assert::AreEqual(2120,selected.sourceBounds.bottom,
                L"A fresh caption replacement must remain visible beyond the independently expanded native picture.");
            Assert::AreEqual(256,u.nativeCrop.top);Assert::AreEqual(1904,u.nativeCrop.bottom);
            auto failed=u;failed.frameSubmitted=false;
            const auto unchanged=CommitSubmittedNativeScopeAnchor(failed);
            Assert::AreEqual(2140,unchanged.captionPreservation.extent.bottom);
            Assert::AreEqual(uint64_t(10707),unchanged.captionPreservation.sourceSequence);
            const auto committed=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(committed.captionPreservation.available);
            Assert::AreEqual(1300,committed.captionPreservation.extent.left);
            Assert::AreEqual(2030,committed.captionPreservation.extent.top,
                L"The native expanded top must not become a caption retention boundary.");
            Assert::AreEqual(2120,committed.captionPreservation.extent.bottom);
            Assert::AreEqual(uint64_t(11002),committed.captionPreservation.sourceSequence);
            // A later independently accepted contraction may discard obsolete
            // native rows while retaining only the newly submitted cue/padding.
            auto c=Native(11003);c.geometry.top=300;c.geometry.bottom=1800;
            c.acceptedNativeCrop.bounds=c.geometry;c.acceptedNativeCrop.acceptedSourceSequence=11003;
            auto next=Unknown(c,committed);next.crop.latestObservationSupportsCrop=true;
            const auto held=StageCaptionSelection(next,u);
            Assert::AreEqual(300,held.sourceBounds.top);Assert::AreEqual(2120,held.sourceBounds.bottom);
            const auto heldCommit=CommitSubmittedNativeScopeAnchor(u);
            Assert::AreEqual(uint64_t(11002),heldCommit.captionPreservation.sourceSequence);
        }
        TEST_METHOD(ExactPreparedNativePresentationRetiresCaptionOnlyAfterSuccessfulSubmission)
        {
            auto r=Unknown(Native(11002),CaptionAnchor(Native()));
            r.crop.frameLocalPresentationRetentionSafe=true;r.crop.currentVisibleBoundsAvailable=false;
            r.crop.preparedSubtitle={true,r.crop.geometry,r.crop.frameSourceGeneration,r.crop.frameSourceSequence,9};
            Assert::IsTrue(HasCurrentPreparedSubtitlePresentation(r.crop));
            SubmittedNativeScopeAnchorUpdateInput u;
            const auto selected=StageCaptionSelection(r,u);
            Assert::IsTrue(selected.owner==DecisionOwner::TRUSTED_CROP);
            Assert::AreEqual(256,selected.sourceBounds.top);Assert::AreEqual(1904,selected.sourceBounds.bottom);
            auto failed=u;failed.frameSubmitted=false;
            const auto stillHeld=CommitSubmittedNativeScopeAnchor(failed);
            Assert::IsTrue(stillHeld.captionPreservation.available);
            Assert::AreEqual(2140,stillHeld.captionPreservation.extent.bottom);
            auto unknown=Unknown(Native(11003),stillHeld);
            Assert::AreEqual(2140,StageCaptionSelection(unknown,failed).sourceBounds.bottom);
            const auto retired=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(retired.available);Assert::IsFalse(retired.captionPreservation.available);
            unknown=Unknown(Native(11003),retired);
            Assert::AreEqual(1904,StageCaptionSelection(unknown,failed).sourceBounds.bottom);
            for(int fault=0;fault<4;++fault) {
                auto stale=r;
                if(fault==0)--stale.crop.preparedSubtitle.sourceSequence;
                if(fault==1)++stale.crop.preparedSubtitle.sourceGeneration;
                if(fault==2)++stale.crop.preparedSubtitle.presentationEpoch;
                if(fault==3)stale.crop.preparedSubtitle.nativeBase.top+=2;
                Assert::IsFalse(HasCurrentPreparedSubtitlePresentation(stale.crop));
                Assert::AreEqual(2140,StageCaptionSelection(stale,failed).sourceBounds.bottom,
                    L"Stale prepared evidence cannot retire a successfully submitted caption extent.");
            }
        }
        TEST_METHOD(CurrentUiFitCannotCreatePersistentCaptionEvidence)
        {
            const auto c=Native();auto u=Submission(c);auto fit=Evaluate(c);
            fit.sourceBounds.bottom=2140;fit.outwardExpanded=true;fit.owner=DecisionOwner::OUTWARD_FIT;
            ConfirmedFitOwnershipInput q;q.origin=ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY;
            q.current=q.confirmed=q.lowerContent=q.lowerOverlayLike=true;
            q.nativeBase=c.geometry;q.approvedBounds=fit.sourceBounds;
            q.evidenceBounds={1300,2050,2500,2138,3840,2160,0.0,ActivePictureBounds::BarAxes::NONE};
            q.sourceGeneration=c.frameSourceGeneration;q.sourceSequence=c.frameSourceSequence;q.presentationEpoch=9;
            fit.confirmedFitOwnership=QualifyConfirmedFitOwnership(q);
            Assert::IsTrue(fit.confirmedFitOwnership.available);
            const auto admitted=AdmitCropPresentation({},c,fit,9);Assert::IsFalse(admitted.blocked);
            u.selectedAdmission=admitted.state;u.selectedOwner=fit.owner;u.submittedBounds=fit.sourceBounds;
            const auto submitted=CommitSubmittedNativeScopeAnchor(u);
            Assert::IsTrue(submitted.available && submitted.submittedPresentationAvailable);
            Assert::IsFalse(submitted.captionPreservation.available,
                L"Independent UI presentation ownership is not persistent caption evidence.");
        }
    };
}
