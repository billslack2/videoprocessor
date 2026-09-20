#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/MadVRShaderRuntimeState.h>
#include <ActivePictureDecisionTimeline.h>
#include <SceneDetector.h>
#include <vector>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>


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
				Input acquisition = crop;
				acquisition.latestObservationSupportsCrop = true;
				const auto prior = AdmitCropPresentation({}, acquisition, Evaluate(acquisition), 3).state;
				Assert::IsTrue(AdmitCropPresentation(prior, crop, candidate, 3).presentation.applyCrop);
			}
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

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 100);
			Assert::AreEqual(1U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 101);
			Assert::AreEqual(2U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 101);
			Assert::AreEqual(2U, decision.state.confirmations);
			Assert::IsFalse(decision.authoritative);

			decision = ConfirmOutwardPictureTransition(
				decision.state, scope, full, evidence, 7, 102);
			Assert::AreEqual(3U, decision.state.confirmations);
			Assert::IsTrue(decision.authoritative);
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
					Assert::AreEqual(0u, decision.state.revalidationSamples);
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
}
