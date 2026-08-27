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
				0, 274, 3840, 1884, 3840, 2160, 2.3851, ActivePictureBounds::BarAxes::TOP_BOTTOM };
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

		TEST_METHOD(VerticalBarPolicyChoosesDominantTwoEdgeOverlay)
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

			// Direction selection is based on required visibility, not on a fixed
			// preference for subtitles at the bottom.
			input.upperRequiredShift = 205.0f;
			decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(-205.0f, decision.translationPixels, 0.001f);

			// Once the lower subtitle owns presentation, the top overlay cannot
			// reverse it even if the top would otherwise request a larger shift.
			input.bottomTranslationHeld = true;
			decision = EvaluateVerticalBarContent(input);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(decision.action));
			Assert::AreEqual(199.0f, decision.translationPixels, 0.001f);
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
			Assert::IsTrue(retained.reason.find("fit") != std::string::npos);

			Input stale = input;
			stale.verticalTranslationSourceGeneration = 6;
			AssertFullRaster(Evaluate(stale));

			Input fullRaster = input;
			fullRaster.fullRasterPresentationAuthoritative = true;
			AssertFullRaster(Evaluate(fullRaster));
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
			Assert::IsTrue(retained.reason.find("bar refinement") !=
				std::string::npos);

			Input unbounded = input;
			unbounded.barCropRefinementPending = false;
			AssertFullRaster(Evaluate(unbounded));

			Input fullRaster = input;
			fullRaster.fullRasterPresentationAuthoritative = true;
			AssertFullRaster(Evaluate(fullRaster));

			Input stale = input;
			stale.geometrySourceGeneration = 6;
			AssertFullRaster(Evaluate(stale));
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

		TEST_METHOD(FreshOneEdgeEvidenceIntentionallyReplacesHeldFit)
		{
			VerticalBarPresentationUpdateInput update;
			update.previous.action = VerticalBarPresentationAction::FIT;
			update.previous.detectedTop = 80;
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
			const auto state = UpdateVerticalBarPresentation(update);
			Assert::AreEqual(static_cast<int>(
				VerticalBarPresentationAction::TRANSLATE),
				static_cast<int>(state.action));
			Assert::AreEqual(104, state.detectedTop);
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

		TEST_METHOD(AspectLimitFillCropsOnlyTrustedWideEnoughContent)
		{
			AspectLimitFillInput input;
			input.trustedAutomaticCropApplied = true;
			input.limitConfigured = true;
			input.aspectLimit = 2.20;
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

			input.trustedAutomaticCropApplied = false;
			const AspectLimitFillDecision untrusted =
				EvaluateAspectLimitFill(input);
			Assert::IsFalse(untrusted.applied);
			Assert::IsTrue(untrusted.reason.find("trusted") != std::string::npos);

			input.trustedAutomaticCropApplied = true;
			input.limitConfigured = false;
			const AspectLimitFillDecision omitted =
				EvaluateAspectLimitFill(input);
			Assert::IsFalse(omitted.applied);
			Assert::IsTrue(omitted.reason.find("not configured") != std::string::npos);
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
	};
}
