#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <cmath>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
        struct ReversalFixture
        {
            NearBlackPresentationEpisodeInput episode;
            CropPresentationAdmissionState admission;
            EstablishedSubtitleTranslationInput translation;

            explicit ReversalFixture(float target = -158.0f)
            {
                episode.trustedCrop = {0,268,3840,1892,3840,2160,2.3645,
                    ActivePictureBounds::BarAxes::TOP_BOTTOM};
                episode.sourceGeneration = episode.retentionSourceGeneration = 2;
                episode.sourceSequence = episode.retentionSourceSequence = 101;
                episode.presentationEpoch = 4;
                episode.currentTick = 1000;
                episode.framesPerSecond = 23.976;
                episode.measurementCurrent = episode.nearBlackEvaluated = true;
                episode.globalNearBlack = episode.trustedCropAvailable = true;
                episode.trustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
                episode.retentionEvaluated = episode.currentObservationAvailable = true;
                episode.currentObservationClassification = ActivePictureClassification::PROVISIONAL;
                episode.currentObservation = episode.retentionBounds = episode.trustedCrop;
                episode.boundedVisibleContentOutsideCrop = true;
                admission.available = true;
                admission.trustedCrop = episode.trustedCrop;
                admission.sourceGeneration = episode.sourceGeneration;
                admission.presentationEpoch = episode.presentationEpoch;
                translation.base = translation.visibleBounds = episode.trustedCrop;
                if (target < 0) translation.visibleBounds.top = 149;
                else translation.visibleBounds.bottom = 2024;
                translation.sourceGeneration = episode.sourceGeneration;
                translation.holdMs = 5000;
                translation.presentation.action = VerticalBarPresentationAction::TRANSLATE;
                translation.presentation.translationPixels = target;
                translation.presentation.sourceSequence = 100;
                translation.presentation.lastDetectionTick = 1000;
            }

            void Resolve(VerticalTranslationDrift& drift, float target, uint64_t tick)
            {
                episode.currentTick = tick;
                translation.resolvedTranslationPixels = static_cast<int>(drift.Resolve(target, tick, 500));
                translation.engagingTranslation = drift.IsActive();
                translation.engageIntervalAvailable = translation.engagingTranslation &&
                    drift.TryGetActiveInterval(translation.engageStartTranslationPixels,
                        translation.engageTargetTranslationPixels,
                        translation.engageAppliedTranslationPixels);
            }

            bool Certificate() const
            {
                return CanRetainEstablishedScopeForSubtitle(episode, admission, translation);
            }
        };

        ReversalFixture ActiveReversal()
        {
            ReversalFixture fixture;
            VerticalTranslationDrift drift;
            drift.Resolve(132.0f, 900, 0);
            fixture.Resolve(drift, -158.0f, 1000);
            fixture.Resolve(drift, -158.0f, 1100);
            return fixture;
        }
    }

    TEST_CLASS(SubtitleTranslationReversalTests)
    {
    public:
        TEST_METHOD(ConfirmedReversalsKeepAdmittedScopeThroughBothSidesAndZero)
        {
            for (int direction : {-1, 1})
            {
                const float start = direction < 0 ? 132.0f : -158.0f;
                const float target = direction < 0 ? -158.0f : 174.0f;
                ReversalFixture fixture(target);
                VerticalTranslationDrift drift;
                drift.Resolve(start, 900, 0);
                bool sawOldSide = false, sawNewSide = false;
                for (int step = 0; step <= 10; ++step)
                {
                    fixture.episode.sourceSequence = fixture.episode.retentionSourceSequence = 101 + step;
                    fixture.Resolve(drift, target, 1000 + step * 50);
                    sawOldSide |= fixture.translation.resolvedTranslationPixels * start > 0;
                    sawNewSide |= fixture.translation.resolvedTranslationPixels * target > 0;
                    Assert::IsTrue(fixture.Certificate());
                    fixture.episode.establishedScopeSubtitleRetained = fixture.Certificate();
                    const auto decision = EvaluateNearBlackPresentationEpisode(fixture.episode);
                    Assert::IsTrue(decision.state.mode == NearBlackPresentationMode::INACTIVE);
                    fixture.episode.previous = decision.state;
                }
                Assert::IsTrue(sawOldSide && sawNewSide);
                Assert::IsFalse(drift.IsActive());
            }

            // An exact zero crossing has the same target-owned engage contract.
            ReversalFixture zero(-158.0f);
            VerticalTranslationDrift drift;
            drift.Resolve(158.0f, 900, 0);
            zero.Resolve(drift, -158.0f, 1000);
            zero.Resolve(drift, -158.0f, 1250);
            Assert::AreEqual(0, zero.translation.resolvedTranslationPixels);
            Assert::IsTrue(zero.Certificate());
        }

        TEST_METHOD(ReversalRejectsMissingMismatchedOrUnboundedDriftCertificate)
        {
            for (int variant = 0; variant < 9; ++variant)
            {
                auto fixture = ActiveReversal();
                Assert::IsTrue(fixture.Certificate());
                auto& t = fixture.translation;
                switch (variant)
                {
                case 0: t.engageIntervalAvailable = false; break;
                case 1: t.engageTargetTranslationPixels = -100.0f; break;
                case 2: t.engageAppliedTranslationPixels = 150.0f; t.resolvedTranslationPixels = 150; break;
                case 3: t.engageAppliedTranslationPixels = -170.0f; t.resolvedTranslationPixels = -170; break;
                case 4: t.resolvedTranslationPixels += 3; break;
                case 5: t.engageStartTranslationPixels = 300.0f; break;
                case 6: t.engageStartTranslationPixels = std::numeric_limits<float>::quiet_NaN(); break;
                case 7: t.engageAppliedTranslationPixels = std::numeric_limits<float>::infinity(); break;
                case 8: t.engageTargetTranslationPixels = std::numeric_limits<float>::quiet_NaN(); break;
                }
                Assert::IsFalse(fixture.Certificate());
            }
        }

        TEST_METHOD(ReversalCannotBypassCurrentPixelsAdmissionOrOwnerLifetime)
        {
            for (int variant = 0; variant < 12; ++variant)
            {
                auto fixture = ActiveReversal();
                Assert::IsTrue(fixture.Certificate());
                switch (variant)
                {
                case 0: fixture.episode.currentTick = 6001; break;
                case 1: fixture.translation.presentation = {}; break;
                case 2: fixture.translation.engagingTranslation = false; break;
                case 3: fixture.translation.competingPresentation = true; break;
                case 4: fixture.admission.available = false; break;
                case 5: ++fixture.admission.presentationEpoch; break;
                case 6: --fixture.episode.retentionSourceSequence; break;
                case 7: fixture.translation.visibleBounds.bottom += 1; break;
                case 8: fixture.translation.visibleBounds.top = 109; break;
                case 9: fixture.episode.currentObservation.left = -1; break;
                case 10: fixture.episode.previous.mode = NearBlackPresentationMode::FULL_RASTER; break;
                case 11: fixture.episode.sceneBoundary = true; break;
                }
                Assert::IsFalse(fixture.Certificate());
            }
        }
    };
}
