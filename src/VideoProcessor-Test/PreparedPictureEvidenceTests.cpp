#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/PreparedPictureEvidence.h>

#include <functional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
    namespace
    {
        struct OwnedProofFrame
        {
            std::shared_ptr<const std::vector<uint16_t>> pixels;
            std::shared_ptr<const int> raw = std::make_shared<const int>(1);
            PreparedAnalysisFrame prepared;
            ActivePictureFrameIdentity identity{ 7, 100, 1000, 123456, 17, 19, 23, 29 };
            ActivePictureBounds base{ 0, 6, 320, 174, 320, 180, 320.0/168.0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM };

            OwnedProofFrame()
            {
                std::vector<uint16_t> words(320*180*2, static_cast<uint16_t>(512 << 6));
                for (int y=0; y<180; ++y)
                    for (int x=0; x<320; ++x)
                        words[y*320+x] = static_cast<uint16_t>((y<22 || y>=158 ? 64 : 300) << 6);
                pixels = std::make_shared<const std::vector<uint16_t>>(std::move(words));
                prepared.source.data = reinterpret_cast<const uint8_t*>(pixels->data());
                prepared.source.dataBytes = pixels->size()*sizeof(uint16_t);
                prepared.source.width = 320; prepared.source.height = 180;
                prepared.source.rowBytes = prepared.source.chromaRowBytes = 640;
                prepared.source.format = AnalysisLumaFormat::P010;
                prepared.source.generation = identity.sourceFormatGeneration;
                prepared.source.backingOwner = pixels;
                prepared.sourceOwner = pixels;
                prepared.domain = AnalysisFrameDomain::ConvertedP010;
                prepared.profile = AnalysisSourceProfile::QueuedCapture;
                prepared.domainKey = identity.analysisDomainKey;
                prepared.sampleRange = VideoFrameSampleRange::LIMITED;
                prepared.colorDepth = 10; prepared.bitShift = 6;
            }

            PreparedPictureEvidenceKey Key(PreparedPictureEvidenceKind kind = PreparedPictureEvidenceKind::Retention) const
            {
                AlphaSourceCrop::InwardCaptionReference caption;
                if (kind == PreparedPictureEvidenceKind::InwardCaption)
                {
                    caption.valid = true; caption.base = base;
                    caption.candidate = caption.remembered = { 0,22,320,158,320,180,320.0/136.0,
                        ActivePictureBounds::BarAxes::TOP_BOTTOM };
                }
                return MakePreparedPictureEvidenceKey(kind, prepared, identity, base, 31, 37, raw, caption);
            }
        };

        void Publish(const std::shared_ptr<PreparedPictureEvidenceWorker::Slot>& slot,
            const PreparedPictureEvidenceKey& key, bool valid = false)
        {
            PreparedPictureEvidence result; result.key = key;
            result.retention.analysisValid = result.retention.presentationValid = valid;
            result.relativeContrast.evaluated = true; result.relativeContrast.valid = valid;
            result.caption.valid = valid;
            std::atomic_store(&slot->ready, std::make_shared<const PreparedPictureEvidence>(std::move(result)));
        }

        struct Event
        {
            HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            ~Event() { if (handle) CloseHandle(handle); }
        };
        struct ReleaseOnExit
        {
            HANDLE handle;
            ~ReleaseOnExit() { SetEvent(handle); }
        };
    }

    TEST_CLASS(PreparedPictureEvidenceTests)
    {
    public:
        TEST_METHOD(CompleteNegativeIsReusableButPendingNeverBecomesEvidence)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            for (auto kind : { PreparedPictureEvidenceKind::Retention,
                PreparedPictureEvidenceKind::RelativeContrast, PreparedPictureEvidenceKind::InwardCaption })
            {
                const auto key = frame.Key(kind);
                const auto slot = cache.Request(key);
                Assert::IsNotNull(slot.get());
                slot->pending.store(true);
                Assert::IsNull(cache.Lookup(key).get());
                Assert::IsTrue(cache.Request(key) == slot);
                Publish(slot, key); // A completed rejection is different from unavailable work.
                const auto complete = cache.Lookup(key);
                Assert::IsNotNull(complete.get());
                Assert::IsFalse(complete->retention.analysisValid);
                Assert::IsFalse(complete->relativeContrast.valid);
                Assert::IsFalse(complete->caption.valid);
                Assert::IsTrue(cache.Request(key) == slot);
            }
        }

        TEST_METHOD(LateEvictedPublicationCannotReplaceNewContextAndKindsDoNotEvictEachOther)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            const auto first = frame.Key(); auto second = first, third = first;
            ++second.policyGeneration; ++third.continuityGeneration;
            const auto old = cache.Request(first); old->pending.store(true);
            Publish(cache.Request(second), second, true);
            const auto relative = frame.Key(PreparedPictureEvidenceKind::RelativeContrast);
            Publish(cache.Request(relative), relative, true);
            const auto replacement = cache.Request(third);
            Assert::IsTrue(replacement != old);
            Publish(old, first, true); // An already-running obsolete job can finish safely.
            Assert::IsNull(cache.Lookup(first).get());
            Assert::IsNull(cache.Lookup(third).get());
            Assert::IsNotNull(cache.Lookup(second).get());
            Assert::IsNotNull(cache.Lookup(relative).get());
            Publish(replacement, third, true);
            Assert::IsNotNull(cache.Lookup(third).get());
        }

        TEST_METHOD(ExactIdentityInterpretationBaseAndInputRevisionAreRequired)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            const auto original = frame.Key(); Publish(cache.Request(original), original, true);
            const std::vector<std::function<void(PreparedPictureEvidenceKey&)>> changes{
                [](auto& k){ ++k.identity.transportGeneration; },
                [](auto& k){ ++k.identity.acceptedSequence; },
                [](auto& k){ ++k.identity.sourceFrameNumber; },
                [](auto& k){ ++k.identity.captureTimestamp; },
                [](auto& k){ ++k.identity.sourceFormatGeneration; },
                [](auto& k){ ++k.identity.viewportGeneration; },
                [](auto& k){ ++k.identity.rendererGeneration; },
                [](auto& k){ ++k.identity.analysisDomainKey; },
                [](auto& k){ ++k.policyGeneration; },
                [](auto& k){ ++k.continuityGeneration; },
                [](auto& k){ ++k.base.top; },
                [](auto& k){ k.base.trustedBarAxes = ActivePictureBounds::BarAxes::NONE; },
                [](auto& k){ k.base.aspectRatio += 0.001; },
                [](auto& k){ ++k.frame.source.data; },
                [](auto& k){ --k.frame.source.dataBytes; },
                [](auto& k){ ++k.frame.source.rowBytes; },
                [](auto& k){ ++k.frame.source.chromaRowBytes; },
                [](auto& k){ k.frame.source.format = AnalysisLumaFormat::P210; },
                [](auto& k){ k.frame.source.encoding = VideoFrameEncoding::V210; },
                [](auto& k){ k.frame.source.colorspace = ColorSpace::REC_709; },
                [](auto& k){ ++k.frame.source.generation; },
                [](auto& k){ k.frame.source.range = AnalysisLumaRange::Full; },
                [](auto& k){ ++k.frame.source.rgbBitAlignment; },
                [](auto& k){ k.frame.domain = AnalysisFrameDomain::ConvertedP210; },
                [](auto& k){ k.frame.profile = AnalysisSourceProfile::LiveRenderer; },
                [](auto& k){ ++k.frame.domainKey; },
                [](auto& k){ ++k.frame.transferKey; },
                [](auto& k){ k.frame.sampleRange = VideoFrameSampleRange::FULL; },
                [](auto& k){ ++k.frame.colorDepth; },
                [](auto& k){ ++k.frame.bitShift; },
                [](auto& k){ ++k.frame.rgbBitAlignment; },
                [](auto& k){ k.frame.dolbyFullRange = !k.frame.dolbyFullRange; },
                [](auto& k){ k.frame.usesConverted = !k.frame.usesConverted; },
                [](auto& k){ k.rawOwner = std::make_shared<const int>(1); },
                [](auto& k){ k.frame.sourceOwner = std::make_shared<const int>(1); },
                [](auto& k){ k.frame.source.backingOwner = std::make_shared<const int>(1); },
            };
            for (const auto& change : changes)
            {
                auto changed = original; change(changed);
                Assert::IsFalse(SamePreparedPictureEvidenceKey(original, changed));
                Assert::IsNull(cache.Lookup(changed).get());
            }
            Assert::IsNotNull(cache.Lookup(original).get());
        }

        TEST_METHOD(SamePointerWithDifferentSharedOwnershipCannotAliasEvidence)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            const auto original = frame.Key(); Publish(cache.Request(original), original, true);
            auto changed = original;
            changed.frame.sourceOwner = std::shared_ptr<const void>(original.frame.sourceOwner.get(), [](const void*){});
            Assert::IsFalse(SamePreparedPictureEvidenceKey(original, changed));
            Assert::IsNull(cache.Lookup(changed).get());
            changed = original;
            changed.rawOwner = std::shared_ptr<const void>(original.rawOwner.get(), [](const void*){});
            Assert::IsNull(cache.Lookup(changed).get());
        }

        TEST_METHOD(CaptionNominationGeometryIsPartOfTheKey)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            const auto original = frame.Key(PreparedPictureEvidenceKind::InwardCaption);
            Publish(cache.Request(original), original, true);
            auto changed = original; ++changed.caption.remembered.top;
            Assert::IsNull(cache.Lookup(changed).get());
            changed = original; ++changed.caption.candidate.bottom;
            Assert::IsNull(cache.Lookup(changed).get());
            changed = original; changed.caption.valid = false;
            Assert::IsNull(cache.Request(changed).get());
            Assert::IsNotNull(cache.Lookup(original).get());
        }

        TEST_METHOD(BorrowedOrMismatchedInputsAreNotCacheable)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            const auto valid = frame.Key();
            auto key = valid; key.frame.sourceOwner.reset();
            Assert::IsNull(cache.Request(key).get());
            key = valid; key.rawOwner.reset();
            Assert::IsNull(cache.Request(key).get());
            key = valid; ++key.frame.source.generation;
            Assert::IsNull(cache.Request(key).get());
            key = valid; ++key.identity.analysisDomainKey;
            Assert::IsNull(cache.Request(key).get());
            key = valid; key.kind = PreparedPictureEvidenceKind::Count;
            Assert::IsNull(cache.Request(key).get());
            key = valid; key.base.bottom = key.base.top;
            Assert::IsNull(cache.Request(key).get());
        }

        TEST_METHOD(CacheRetainsImmutableOwnersAndClearReleasesThem)
        {
            PreparedPictureEvidenceCache cache;
            std::weak_ptr<const std::vector<uint16_t>> pixels;
            std::weak_ptr<const int> raw;
            {
                OwnedProofFrame frame; pixels = frame.pixels; raw = frame.raw;
                const auto key = frame.Key();
                Publish(cache.Request(key), key, true);
            }
            Assert::IsFalse(pixels.expired()); Assert::IsFalse(raw.expired());
            cache.Clear();
            Assert::IsTrue(pixels.expired()); Assert::IsTrue(raw.expired());
        }

        TEST_METHOD(EvaluatorPreservesPixelSafetyAndBindsRelativeProofToTransport)
        {
            OwnedProofFrame frame;
            const auto raw = ExtractActivePictureEvidence(frame.prepared.source);
            const auto darkness = EvaluateActivePictureArDarkness(frame.prepared.source, raw);
            const auto key = frame.Key();
            const auto result = EvaluatePreparedPictureEvidence(key, raw, darkness);
            const auto direct = EvaluateActivePicturePresentationRetention(frame.prepared.source,
                frame.base, raw, darkness.global, darkness);
            Assert::IsTrue(SamePreparedPictureEvidenceKey(key, result.key));
            Assert::IsTrue(result.retention.analysisValid == direct.analysisValid);
            Assert::IsTrue(result.retention.presentationValid == direct.presentationValid);
            Assert::IsTrue(result.retention.CanRetainPresentation() == direct.CanRetainPresentation());
            Assert::IsTrue(result.retention.excludedBandsPixelSafe == direct.excludedBandsPixelSafe);
            Assert::IsTrue(result.retention.expansionStripsAvailable == direct.expansionStripsAvailable);
            const auto relativeKey = frame.Key(PreparedPictureEvidenceKind::RelativeContrast);
            const auto relative = EvaluatePreparedPictureEvidence(relativeKey, raw, darkness);
            Assert::IsTrue(relative.relativeContrast.evaluated);
            Assert::IsTrue(relative.relativeContrast.sourceGeneration == frame.identity.transportGeneration);
            Assert::IsTrue(relative.key.frame.source.generation == frame.identity.sourceFormatGeneration,
                L"The retained queued source is immutable; only the independent relative proof is rebound.");
        }

        TEST_METHOD(EvaluatorRejectsInvalidInputsAndCancellationWithoutReturningEvidence)
        {
            OwnedProofFrame frame;
            const auto raw = ExtractActivePictureEvidence(frame.prepared.source);
            const auto darkness = EvaluateActivePictureArDarkness(frame.prepared.source, raw);
            auto key = frame.Key(); key.rawOwner.reset();
            Assert::ExpectException<std::invalid_argument>([&] {
                (void)EvaluatePreparedPictureEvidence(key, raw, darkness);
            });
            AnalysisEvaluationBudget cancelled([](void*) { return true; }, nullptr);
            for (auto kind : { PreparedPictureEvidenceKind::Retention,
                PreparedPictureEvidenceKind::RelativeContrast, PreparedPictureEvidenceKind::InwardCaption })
            {
                const auto valid = frame.Key(kind);
                Assert::ExpectException<AnalysisEvaluationCancelled>([&] {
                    (void)EvaluatePreparedPictureEvidence(valid, raw, darkness, &cancelled);
                });
            }
        }
        TEST_METHOD(WrongReturnedContextIsNotAdmitted)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            const auto requested = frame.Key(); auto returned = requested;
            ++returned.policyGeneration;
            Publish(cache.Request(requested), returned, true);
            Assert::IsNull(cache.Lookup(requested).get());
        }

        TEST_METHOD(NewRequestsRemainAvailableWhileAnOldWorkerIsBlocked)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            Event started, release; PreparedPictureEvidenceWorker worker;
            ReleaseOnExit unblock{ release.handle };
            const auto key = frame.Key(); const auto old = cache.Request(key);
            Assert::IsTrue(worker.Submit(old, 7, 100, 100, [&] {
                SetEvent(started.handle); WaitForSingleObject(release.handle, 5000);
                PreparedPictureEvidence result; result.key = key; return result;
            }));
            Assert::IsTrue(WaitForSingleObject(started.handle, 2000) == WAIT_OBJECT_0);
            auto second = key, third = key; ++second.policyGeneration; ++third.continuityGeneration;
            cache.Request(second); const auto replacement = cache.Request(third);
            Assert::IsTrue(old->pending.load());
            Assert::IsNotNull(replacement.get()); Assert::IsTrue(replacement != old);
            SetEvent(release.handle); worker.Stop();
            Assert::IsNull(cache.Lookup(third).get());
            Assert::IsNull(cache.Lookup(key).get());
        }

        TEST_METHOD(CancelledLaterComponentRetainsAlreadyCompletedIndependentWork)
        {
            OwnedProofFrame frame; PreparedPictureEvidenceCache cache;
            const auto retention = frame.Key();
            const auto raw = ExtractActivePictureEvidence(frame.prepared.source);
            const auto darkness = EvaluateActivePictureArDarkness(frame.prepared.source, raw);
            PreparedPictureEvidence complete; complete.key = retention;
            complete.retention = EvaluateActivePicturePresentationRetention(frame.prepared.source,
                frame.base, raw, darkness.global, darkness);
            std::atomic_store(&cache.Request(retention)->ready,
                std::make_shared<const PreparedPictureEvidence>(std::move(complete)));
            const auto retained = cache.Lookup(retention); Assert::IsNotNull(retained.get());
            const auto relative = frame.Key(PreparedPictureEvidenceKind::RelativeContrast);
            auto pending = cache.Request(relative);
            Event attempted; PreparedPictureEvidenceWorker worker;
            Assert::IsTrue(worker.Submit(pending, 7, 100, 100, [&]() -> PreparedPictureEvidence {
                SetEvent(attempted.handle);
                AnalysisEvaluationBudget budget([](void*) { return true; }, nullptr);
                PreparedPictureEvidence result; result.key = relative;
                result = EvaluatePreparedPictureEvidence(relative, raw, darkness, &budget);
                return result;
            }));
            Assert::IsTrue(WaitForSingleObject(attempted.handle, 2000) == WAIT_OBJECT_0);
            worker.Stop();
            Assert::IsTrue(cache.Lookup(retention) == retained);
            Assert::IsNull(cache.Lookup(relative).get());
            Assert::IsFalse(pending->pending.load());
            Assert::IsTrue(worker.Snapshot().failed == 1);
        }
    };
}