#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/MadVRShaderRuntimeState.h>
#include <vprenderer/AlphaSourceCropPolicy.h>


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
				0, 274, 3840, 1884, 3840, 2160, 2.3851, true };
			input.geometrySourceGeneration = 7;
			input.frameSourceGeneration = 7;
			input.rasterWidth = 3840;
			input.rasterHeight = 2160;
			return input;
		}

		void AssertFullRaster(const Decision& decision)
		{
			Assert::IsFalse(decision.applyCrop);
			Assert::AreEqual(0, decision.sourceBounds.left);
			Assert::AreEqual(0, decision.sourceBounds.top);
			Assert::AreEqual(3840, decision.sourceBounds.right);
			Assert::AreEqual(2160, decision.sourceBounds.bottom);
		}
	}

	TEST_CLASS(AlphaSourceCropPolicyTests)
	{
	public:
		TEST_METHOD(BarContentDetectorNeverManufacturesAnOppositeEdge)
		{
			Assert::AreEqual(static_cast<int>(BarContentEdge::TOP),
				static_cast<int>(SelectVerticalBarContentEdge(170.0f, 0.0f)));
			Assert::AreEqual(static_cast<int>(BarContentEdge::BOTTOM),
				static_cast<int>(SelectVerticalBarContentEdge(0.0f, 116.0f)));

			// A receiver/menu item can leave weaker noise on the other bar. Only
			// the edge that actually determines the fit may acquire a hold timer.
			Assert::AreEqual(static_cast<int>(BarContentEdge::TOP),
				static_cast<int>(SelectVerticalBarContentEdge(170.0f, 116.0f)));
			Assert::AreEqual(static_cast<int>(BarContentEdge::BOTTOM),
				static_cast<int>(SelectVerticalBarContentEdge(42.0f, 116.0f)));
			Assert::AreEqual(static_cast<int>(BarContentEdge::NONE),
				static_cast<int>(SelectVerticalBarContentEdge(0.5f, 0.5f)));
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
			const MadVRNlsMappingDecision mapping = EvaluateMadVRNlsMapping(
				true, finalAspect, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(
				static_cast<int>(MadVRNlsMappingMode::SCOPE_PASSTHROUGH),
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

		TEST_METHOD(SubtitleOverlayExpandsOnlyTheRequiredOuterEdge)
		{
			Input input = TrustedScopeCrop();
			input.outwardPresentationActive = true;
			input.outwardExpansionAvailable = true;
			input.outwardExpansion = input.geometry;
			input.outwardExpansion.bottom = 2100;
			input.outwardExpansion.aspectRatio = 3840.0 / (2100 - 274);
			input.outwardExpansionSourceGeneration = 7;
			const Decision decision = Evaluate(input);
			Assert::IsTrue(decision.applyCrop);
			Assert::IsTrue(decision.outwardExpanded);
			Assert::AreEqual(274, decision.sourceBounds.top);
			Assert::AreEqual(2100, decision.sourceBounds.bottom);

			const double finalAspect = static_cast<double>(
				decision.sourceBounds.right - decision.sourceBounds.left) /
				(decision.sourceBounds.bottom - decision.sourceBounds.top);
			const MadVRNlsMappingDecision mapping = EvaluateMadVRNlsMapping(
				true, finalAspect, 2.35, 5.0, 1.0, false);
			Assert::AreEqual(static_cast<int>(MadVRNlsMappingMode::ACTIVE),
				static_cast<int>(mapping.mode));
			Assert::AreEqual(finalAspect, mapping.sourceAspect, 0.000001);
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
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, false };
			full.outwardExpansionSourceGeneration = 7;
			const Decision fullDecision = Evaluate(full);
			Assert::IsTrue(fullDecision.outwardExpanded);
			Assert::AreEqual(0, fullDecision.sourceBounds.top);
			Assert::AreEqual(2160, fullDecision.sourceBounds.bottom);

			Input pillar = TrustedScopeCrop();
			pillar.geometry = {
				480, 0, 3360, 2160, 3840, 2160, 4.0 / 3.0, true };
			pillar.outwardPresentationActive = true;
			pillar.outwardExpansionAvailable = true;
			pillar.outwardExpansion = {
				120, 0, 3700, 2160, 3840, 2160,
				3580.0 / 2160.0, false };
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
				480, 0, 3360, 2160, 3840, 2160, 4.0 / 3.0, true };
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

		TEST_METHOD(AsymmetricBoundsCannotAcquireCropAuthority)
		{
			Input input = TrustedScopeCrop();
			input.geometry.symmetricBars = false;
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
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, true };
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
				static_cast<int>(ScenePresentationAction::WITHDRAW),
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
				static_cast<int>(ScenePresentationAction::WITHDRAW),
				static_cast<int>(EvaluateSceneBoundary(input).action));
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
				static_cast<int>(ScenePresentationAction::WITHDRAW),
				static_cast<int>(EvaluateSceneBoundary(input).action));
		}

		TEST_METHOD(SceneBoundaryWithdrawsUnavailableContradictoryOrStaleState)
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
				static_cast<int>(ScenePresentationAction::WITHDRAW),
				static_cast<int>(EvaluateSceneBoundary(input).action));

			input.latestClassification =
				ActivePictureClassification::UNAVAILABLE;
			Assert::AreEqual(
				static_cast<int>(ScenePresentationAction::WITHDRAW),
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
			Assert::IsFalse(decision.cropActive);
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
	};
}
