#include "pch.h"
#include "CppUnitTest.h"

#include <ActivePictureEvidence.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <AnalysisDiagnosticTrace.h>
#include <SubtitleMeasurementWorker.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    namespace
    {
        // This harness replays production measurement and transition components
        // from fresh state. It does not reconstruct the renderer's earlier
        // history, asynchronous scheduling, subtitle presentation or crop episode.
        struct ReplayFrame
        {
            std::shared_ptr<std::vector<uint8_t>> pixels;
            AnalysisLumaSource source;
            ActivePictureFrameIdentity identity;
            uint64_t policy = 0, continuity = 0, frameDuration100ns = 0;
            int nearBarDistance = 0, optimizationMode = 0;
            SubtitleScriptHint script = SubtitleScriptHint::Latin;
            bool discontinuity = false, cadenceRepeat = false;
        };

        struct ReplayResult
        {
            ActivePictureTransitionDecision decision;
            ActivePictureClassification rawClass = ActivePictureClassification::UNAVAILABLE;
            ActivePictureClassification logicalClass = ActivePictureClassification::UNAVAILABLE;
            ActivePictureBounds rawBounds, logicalBounds;
            uint64_t sequence = 0;
            int lines = 0;
            unsigned captionObservations = 0;
            bool captionConfirmed = false;
            ActivePictureBounds captionBounds;
            bool rawDark = false, arDark = false, subtitleProof = false;
            bool reset = false, repeated = false, arOnlyGate = false;
        };

        bool SameReplayContext(const ReplayFrame& a, const ReplayFrame& b)
        {
            const auto& x = a.identity;
            const auto& y = b.identity;
            return x.transportGeneration == y.transportGeneration &&
                x.sourceFormatGeneration == y.sourceFormatGeneration &&
                x.analysisDomainKey == y.analysisDomainKey &&
                x.viewportGeneration == y.viewportGeneration &&
                x.rendererGeneration == y.rendererGeneration &&
                a.source.width == b.source.width && a.source.height == b.source.height &&
                a.source.rowBytes == b.source.rowBytes && a.source.chromaRowBytes == b.source.chromaRowBytes &&
                a.source.format == b.source.format && a.source.encoding == b.source.encoding &&
                a.source.colorspace == b.source.colorspace &&
                a.source.rgbBitAlignment == b.source.rgbBitAlignment &&
                a.source.range == b.source.range &&
                a.policy == b.policy && a.continuity == b.continuity &&
                a.script == b.script && a.nearBarDistance == b.nearBarDistance &&
                a.optimizationMode == b.optimizationMode &&
                a.frameDuration100ns == b.frameDuration100ns;
        }

        class FreshReplay
        {
        public:
            explicit FreshReplay(bool requireArOnly = false) : m_requireArOnly(requireArOnly) {}
            ReplayResult Observe(const ReplayFrame& frame)
            {
                Assert::IsTrue(frame.source.IsValid());
                Assert::IsTrue(frame.identity.transportGeneration != 0 &&
                    frame.identity.acceptedSequence != 0 && frame.frameDuration100ns != 0);
                const bool sameContext = m_hasPrevious && SameReplayContext(frame, m_previous);
                const bool exactRepeat = sameContext &&
                    SameAnalysisFrameIdentity(frame.identity, m_previous.identity);
                ReplayResult result;
                result.sequence = frame.identity.acceptedSequence;
                if (exactRepeat)
                {
                    // Display repeats never become another source observation.
                    result = m_last;
                    result.decision.publish = false;
                    result.reset = false;
                    result.repeated = true;
                    return result;
                }

                const bool gap = sameContext &&
                    (m_previous.identity.acceptedSequence == UINT64_MAX ||
                        frame.identity.acceptedSequence != m_previous.identity.acceptedSequence + 1);
                result.reset = !sameContext || gap || frame.discontinuity;
                if (result.reset)
                {
                    m_model.Reset();
                    m_prior = {};
                    m_sampler = std::make_unique<SubtitleMeasurementSampler>();
                    m_captionPresentation.Reset();
                    m_last = {};
                }
                // A repeat without its original frame in this bounded capture
                // cannot contribute a fresh vote. Keep the gap explicit.
                if (frame.cadenceRepeat)
                {
                    result.repeated = true;
                    result.decision = m_last.decision;
                    result.decision.publish = false;
                    SavePrevious(frame, result);
                    return result;
                }

                const auto raw = ExtractActivePictureEvidence(frame.source);
                result.rawClass = raw.classification;
                result.rawBounds = raw.trustedBounds;
                if (m_requireArOnly)
                {
                    result.arOnlyGate = AlphaSourceCrop::ColdCaptionArAnalysisEligible(
                        true, true, false, false,
                        AlphaSourceCrop::NearBlackPresentationMode::FULL_RASTER);
                    Assert::IsTrue(result.arOnlyGate,
                        L"Off-mode Scope AR must enqueue the current worker before measuring this source.");
                }
                SubtitleBoxObservation key;
                key.identity = frame.identity;
                key.width = frame.source.width; key.height = frame.source.height;
                key.policyGeneration = frame.policy;
                key.continuityGeneration = frame.continuity;
                key.nearBarDistance = frame.nearBarDistance;
                key.optimizationMode = frame.optimizationMode;
                key.scriptHint = frame.script;
                key.discontinuity = frame.discontinuity;
                // Fresh acquisition deliberately supplies no invented live
                // presentation rectangle or historical subtitle seed.
                key.sharedPicture.required = true;
                key.sharedPicture.identity = frame.identity;
                const double fps = 10000000.0 / double(frame.frameDuration100ns);
                const auto measured = m_sampler->Measure(frame.source, key, m_prior, fps, raw);
                m_prior = SubtitleBoxLookahead::AdvanceBarTrackingReference(measured);
                result.lines = measured.text.lineCount;
                auto preview = SubtitleBoxLookahead::Resolve(&measured, 1,
                    frame.policy, frame.continuity);
                m_captionPresentation.RefreshReferencePixels(preview, frame.source);
                const auto caption = m_captionPresentation.Consume(preview, 250,
                    double(frame.frameDuration100ns) / 10000.0);
                result.captionObservations = caption.observations;
                result.captionConfirmed = measured.barAuthority &&
                    SameAnalysisFrameIdentity(measured.identity, frame.identity) &&
                    measured.analyzed && !measured.pendingRefresh &&
                    measured.text.detected && !measured.text.workLimit &&
                    measured.text.nearBarEligibilityMeasured &&
                    measured.text.nearBarEligible && caption.detected && !caption.held &&
                    caption.observations >= 2 && caption.bounds.Valid() &&
                    measured.text.bounds.Valid();
                if (result.captionConfirmed)
                {
                    result.captionBounds = {
                        std::min(caption.bounds.left, measured.text.bounds.left),
                        std::min(caption.bounds.top, measured.text.bounds.top),
                        std::max(caption.bounds.right, measured.text.bounds.right),
                        std::max(caption.bounds.bottom, measured.text.bounds.bottom),
                        frame.source.width, frame.source.height, 1.0,
                        ActivePictureBounds::BarAxes::NONE };
                    if (measured.text.provenSourcePanel.Valid())
                    {
                        result.captionBounds.left = std::min(result.captionBounds.left,
                            measured.text.provenSourcePanel.left);
                        result.captionBounds.top = std::min(result.captionBounds.top,
                            measured.text.provenSourcePanel.top);
                        result.captionBounds.right = std::max(result.captionBounds.right,
                            measured.text.provenSourcePanel.right);
                        result.captionBounds.bottom = std::max(result.captionBounds.bottom,
                            measured.text.provenSourcePanel.bottom);
                    }
                    result.captionBounds.aspectRatio =
                        double(result.captionBounds.right - result.captionBounds.left) /
                        std::max(1, result.captionBounds.bottom - result.captionBounds.top);
                }
                result.subtitleProof = measured.subtitleArProof.IsCurrentFor(frame.source,
                    frame.identity, frame.policy, frame.continuity, frame.script);
                const auto evidence = result.subtitleProof ? measured.subtitleArProof.evidence : raw;
                const auto darkness = EvaluateActivePictureArDarkness(frame.source, evidence);
                result.rawDark = darkness.global.nearBlack;
                result.arDark = darkness.EffectiveNearBlack();
                const auto episodeMode = result.arDark
                    ? AlphaSourceCrop::NearBlackPresentationMode::FULL_RASTER
                    : AlphaSourceCrop::NearBlackPresentationMode::INACTIVE;
                const bool logicalAcquisitionBlocked =
                    AlphaSourceCrop::NearBlackBlocksCaptionBackedLogicalAcquisition(
                        result.arDark, episodeMode, result.subtitleProof);
                const auto admitted = ConstrainNearBlackCropAcquisition(
                    evidence, logicalAcquisitionBlocked);
                result.logicalClass = admitted.classification;
                result.logicalBounds = admitted.trustedBounds;
                result.decision = m_model.Observe(MakeActivePictureObservation(admitted,
                    frame.identity.acceptedSequence, fps));
                SavePrevious(frame, result);
                return result;
            }

        private:
            void SavePrevious(const ReplayFrame& frame, const ReplayResult& result)
            {
                // Retain metadata only; a replay never pins earlier full frames.
                m_previous = frame;
                m_previous.pixels.reset();
                m_previous.source.backingOwner.reset();
                m_previous.source.data = nullptr;
                m_hasPrevious = true;
                m_last = result;
            }
            ActivePictureTransitionModel m_model;
            std::unique_ptr<SubtitleMeasurementSampler> m_sampler;
            SubtitleBoxPresentation m_captionPresentation;
            SubtitleBarTrackingReference m_prior;
            ReplayFrame m_previous;
            ReplayResult m_last;
            bool m_hasPrevious = false;
            bool m_requireArOnly = false;
        };

        ReplayFrame ReadFrame(const std::filesystem::path& path)
        {
            ReplayFrame result;
            auto sidecar = path;
            sidecar.replace_extension(L".txt");
            std::ifstream metadata(sidecar);
            int format = 0, encoding = 0, colorspace = 0, top = 0, bottom = 0, script = 0;
            std::string headerLine;
            std::getline(metadata, headerLine);
            std::istringstream header(headerLine);
            header >> result.source.width >> result.source.height >> result.source.rowBytes >>
                result.source.chromaRowBytes >> format >> encoding >> colorspace >> top >> bottom >> script;
            Assert::IsTrue(bool(header), L"The native frame sidecar is missing or malformed.");
            int range = 0;
            if (header >> range)
            {
                Assert::IsTrue(range >= 0 && range <= 2);
                result.source.range = static_cast<AnalysisLumaRange>(range);
            }
            std::string identityVersion;
            int discontinuity = 0, repeat = 0, alignment = 0;
            metadata >> identityVersion >> result.identity.transportGeneration >>
                result.identity.acceptedSequence >> result.identity.sourceFrameNumber >>
                result.identity.captureTimestamp >> result.identity.sourceFormatGeneration >>
                result.identity.viewportGeneration >> result.identity.rendererGeneration >>
                result.identity.analysisDomainKey >> result.policy >> result.continuity >>
                result.nearBarDistance >> result.optimizationMode >> result.frameDuration100ns >>
                discontinuity >> repeat >> alignment;
            Assert::IsTrue(bool(metadata) && identityVersion == "identity_v1",
                L"Ordered replay requires identity_v1 metadata; it never invents identities for a single-frame fixture.");
            Assert::IsTrue(result.source.width > 0 && result.source.width <= 8192 &&
                result.source.height > 0 && result.source.height <= 4320);
            Assert::IsTrue(format >= int(AnalysisLumaFormat::P010) &&
                format <= int(AnalysisLumaFormat::SdrTunnelRgb));
            Assert::IsTrue(encoding >= int(VideoFrameEncoding::UNKNOWN) &&
                encoding <= int(VideoFrameEncoding::DNxHR));
            Assert::IsTrue(colorspace >= int(ColorSpace::UNKNOWN) && colorspace <= int(ColorSpace::BT_2020));
            Assert::IsTrue(script >= int(SubtitleScriptHint::GeneralMixed) && script <= int(SubtitleScriptHint::Thai));
            Assert::IsTrue(alignment >= 0 && alignment <= 2);
            Assert::IsTrue((discontinuity == 0 || discontinuity == 1) && (repeat == 0 || repeat == 1));
            result.source.format = static_cast<AnalysisLumaFormat>(format);
            result.source.encoding = static_cast<VideoFrameEncoding>(encoding);
            result.source.colorspace = static_cast<ColorSpace>(colorspace);
            result.source.rgbBitAlignment = static_cast<uint8_t>(alignment);
            result.source.generation = result.identity.transportGeneration;
            result.script = static_cast<SubtitleScriptHint>(script);
            result.discontinuity = discontinuity != 0;
            result.cadenceRepeat = repeat != 0;
            std::ifstream pixels(path, std::ios::binary | std::ios::ate);
            Assert::IsTrue(bool(pixels));
            const auto size = pixels.tellg();
            Assert::IsTrue(size > 0 && size <= std::streamoff(128ull * 1024 * 1024),
                L"Native frame exceeds the diagnostic packet limit.");
            result.pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(size));
            pixels.seekg(0);
            pixels.read(reinterpret_cast<char*>(result.pixels->data()), static_cast<std::streamsize>(size));
            Assert::IsTrue(bool(pixels));
            result.source.data = result.pixels->data();
            result.source.dataBytes = result.pixels->size();
            result.source.backingOwner = result.pixels;
            Assert::IsTrue(result.source.IsValid());
            return result;
        }

        std::string Describe(const ReplayResult& result)
        {
            std::ostringstream text;
            text << "Fresh component replay sequence=" << result.sequence << " reset=" << result.reset <<
                " repeat=" << result.repeated << " ar_only_gate=" << result.arOnlyGate <<
                " raw_class=" << int(result.rawClass) <<
                " native=" << result.rawBounds.left << ',' << result.rawBounds.top << '-' <<
                result.rawBounds.right << ',' << result.rawBounds.bottom <<
                " logical_class=" << int(result.logicalClass) << " logical=" <<
                result.logicalBounds.left << "," << result.logicalBounds.top << "-" <<
                result.logicalBounds.right << "," << result.logicalBounds.bottom <<
                " raw_dark=" << result.rawDark << " ar_dark=" << result.arDark <<
                " lines=" << result.lines << " caption_obs=" << result.captionObservations <<
                " caption_confirmed=" << result.captionConfirmed <<
                " caption=" << result.captionBounds.left << "," << result.captionBounds.top << "-" <<
                result.captionBounds.right << "," << result.captionBounds.bottom <<
                " subtitle_ar=" << result.subtitleProof <<
                " publish=" << result.decision.publish << " stable=" << result.decision.stable <<
                " bounds=" << result.decision.bounds.left << ',' << result.decision.bounds.top << '-' <<
                result.decision.bounds.right << ',' << result.decision.bounds.bottom <<
                " votes=" << unsigned(result.decision.matchingCandidates) <<
                " reason=" << result.decision.reason;
            return text.str();
        }

        ReplayFrame SyntheticFrame(uint64_t sequence, bool scope)
        {
            constexpr int width = 640, height = 360;
            ReplayFrame frame;
            frame.pixels = std::make_shared<std::vector<uint8_t>>(width * height * 3, 0);
            auto words = reinterpret_cast<uint16_t*>(frame.pixels->data());
            std::fill(words, words + width * height * 3 / 2, uint16_t(512 << 6));
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const int value = scope && (y < 44 || y >= 316) ? 64 : 220 + ((x / 8 + y / 8) % 7) * 12;
                    words[y * width + x] = uint16_t(value << 6);
                }
            frame.source = { frame.pixels->data(), frame.pixels->size(), width, height,
                width * 2, width * 2, AnalysisLumaFormat::P010,
                VideoFrameEncoding::V210, ColorSpace::REC_709, 1 };
            frame.source.backingOwner = frame.pixels;
            frame.identity = { 1, sequence, sequence, sequence * 417083, 2, 3, 4, 5 };
            frame.policy = 6; frame.continuity = 7;
            frame.frameDuration100ns = 417083;
            frame.nearBarDistance = 20;
            return frame;
        }

        struct ReplayPacketOwner
        {
            std::shared_ptr<const void> owner;
            std::atomic<unsigned> retained{ 0 };
        };

        void* __cdecl RetainReplayPacket(const void* context)
        {
            auto& owner = *const_cast<ReplayPacketOwner*>(static_cast<const ReplayPacketOwner*>(context));
            auto token = new (std::nothrow) std::shared_ptr<const void>(owner.owner);
            if (token) ++owner.retained;
            return token;
        }

        void __cdecl ReleaseReplayPacket(void* token)
        {
            delete static_cast<std::shared_ptr<const void>*>(token);
        }

        struct ReplayCaptureFixture
        {
            std::filesystem::path root;
            std::unique_ptr<AnalysisDiagnosticTrace> trace;
            ReplayCaptureFixture()
            {
                static std::atomic<unsigned> sequence{ 0 };
                root = std::filesystem::temp_directory_path() /
                    ("vp-native-replay-roundtrip-" + std::to_string(GetCurrentProcessId()) + "-" +
                        std::to_string(GetTickCount64()) + "-" + std::to_string(++sequence));
                AnalysisDiagnosticTraceOptions options;
                options.outputRoot = root.string();
                options.captureMs = 10000;
                trace = std::make_unique<AnalysisDiagnosticTrace>(options);
                trace->SetProducerAvailable(true, true);
            }
            ~ReplayCaptureFixture()
            {
                trace.reset(); // Join writer and release owners before removing this test's private directory.
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        };
    }

    TEST_CLASS(AnalysisDiagnosticReplayTests)
    {
    public:
        TEST_METHOD(CapturedNativeSequenceRoundTripsWriterSidecarsAndProductionAcquisition)
        {
            ReplayCaptureFixture fixture;
            Assert::AreEqual(int(AnalysisDiagnosticCaptureResult::Started),
                int(fixture.trace->StartCapture(true)));
            FreshReplay expectedReplay;
            ReplayResult expected;
            for (uint64_t index = 1; index <= 4; ++index)
            {
                auto frame = SyntheticFrame(100 + index, true);
                frame.source.range = AnalysisLumaRange::Limited;
                expected = expectedReplay.Observe(frame);
                ReplayPacketOwner owner;
                owner.owner = frame.pixels;
                AnalysisDiagnosticFramePacketV1 packet;
                packet.width = frame.source.width; packet.height = frame.source.height;
                packet.stride0 = frame.source.rowBytes; packet.stride1 = frame.source.chromaRowBytes;
                packet.format = uint32_t(frame.source.format);
                packet.encoding = uint32_t(frame.source.encoding);
                packet.colorspace = uint32_t(frame.source.colorspace);
                packet.analysisSampleRange = uint32_t(frame.source.range);
                packet.top = 44; packet.bottom = 316; packet.scriptHint = int(frame.script);
                packet.transportGeneration = frame.identity.transportGeneration;
                packet.acceptedSequence = frame.identity.acceptedSequence;
                packet.sourceFrameNumber = frame.identity.sourceFrameNumber;
                packet.captureTimestamp = frame.identity.captureTimestamp;
                packet.sourceFormatGeneration = frame.identity.sourceFormatGeneration;
                packet.viewportGeneration = frame.identity.viewportGeneration;
                packet.rendererGeneration = frame.identity.rendererGeneration;
                packet.analysisDomainKey = frame.identity.analysisDomainKey;
                packet.policyGeneration = frame.policy; packet.continuityGeneration = frame.continuity;
                packet.nearBarDistance = frame.nearBarDistance;
                packet.optimizationMode = frame.optimizationMode;
                packet.frameDuration100ns = frame.frameDuration100ns;
                packet.data = frame.source.data; packet.dataBytes = frame.source.dataBytes;
                packet.ownerContext = &owner;
                packet.retainOwner = RetainReplayPacket; packet.releaseOwner = ReleaseReplayPacket;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                // A try-lock recorder is permitted to drop a submitted packet.
                // Retry only until the ownership callback confirms acceptance.
                do
                {
                    fixture.trace->SubmitFrame(packet);
                    if (owner.retained.load() != 0) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                } while (std::chrono::steady_clock::now() < deadline);
                Assert::AreEqual(1u, owner.retained.load());
                owner.owner.reset(); frame.pixels.reset(); frame.source.backingOwner.reset();
                while (fixture.trace->Status().framesWritten < index &&
                    std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                Assert::AreEqual(index, fixture.trace->Status().framesWritten);
            }
            fixture.trace->StopCapture();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (fixture.trace->Status().captureWriting && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const auto status = fixture.trace->Status();
            Assert::IsFalse(status.captureWriting);
            Assert::AreEqual<uint64_t>(0, status.writeFailures);
            const auto directory = std::filesystem::u8path(status.outputPath);
            FreshReplay actualReplay;
            ReplayResult actual;
            for (uint64_t index = 1; index <= 4; ++index)
            {
                std::ostringstream filename;
                filename << "frame-00000" << index << ".bin";
                const auto loaded = ReadFrame(directory / filename.str());
                Assert::AreEqual(int(AnalysisLumaRange::Limited), int(loaded.source.range));
                auto original = SyntheticFrame(100 + index, true);
                original.source.range = AnalysisLumaRange::Limited;
                Assert::IsTrue(SameAnalysisFrameIdentity(original.identity, loaded.identity));
                Assert::IsTrue(SameReplayContext(original, loaded));
                Assert::IsTrue(*original.pixels == *loaded.pixels);
                actual = actualReplay.Observe(loaded);
                Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED), int(actual.rawClass));
                Assert::AreEqual(44, actual.rawBounds.top);
                Assert::AreEqual(316, actual.rawBounds.bottom);
            }
            Assert::IsTrue(expected.decision.publish && actual.decision.publish && actual.decision.stable);
            Assert::IsTrue(Describe(expected) == Describe(actual));
        }

        TEST_METHOD(OrderedNativeTransitionIsDeterministicWithRollingTraceAndRepeats)
        {
            FreshReplay quiet, diagnostic;
            // The recorder's fixed history is intentionally allocated off the
            // test thread stack, just as the production singleton is.
            AnalysisDiagnosticTraceOptions options;
            options.maxRecordHistory = 32;
            const auto trace = std::make_unique<AnalysisDiagnosticTrace>(options);
            ReplayResult final;
            bool sawFullRaster = false, sawScopePublication = false;
            for (uint64_t sequence = 1; sequence <= 12; ++sequence)
            {
                const auto frame = SyntheticFrame(sequence, sequence > 4);
                const auto first = quiet.Observe(frame);
                const auto second = diagnostic.Observe(frame);
                AnalysisDiagnosticRecordV1 record;
                record.kind = AnalysisDiagnosticRecordKindV1::Frame;
                record.transportGeneration = frame.identity.transportGeneration;
                record.acceptedSequence = frame.identity.acceptedSequence;
                record.sourceFrameNumber = frame.identity.sourceFrameNumber;
                record.captureTimestamp = frame.identity.captureTimestamp;
                record.analysisDomainKey = frame.identity.analysisDomainKey;
                record.resultCode = int(second.rawClass);
                record.published[0] = second.decision.bounds.left;
                record.published[1] = second.decision.bounds.top;
                record.published[2] = second.decision.bounds.right;
                record.published[3] = second.decision.bounds.bottom;
                trace->Submit(record);
                // Diagnostics read the completed result; they do not supply a
                // detector argument or alter its next observation.
                Assert::IsFalse(Describe(second).empty());
                Assert::IsTrue(Describe(first) == Describe(second));
                if (sequence <= 4)
                {
                    Assert::AreEqual(int(ActivePictureClassification::FULL_RASTER_TRUSTED), int(first.rawClass));
                    sawFullRaster |= first.decision.stable && first.decision.bounds.top == 0;
                }
                else
                {
                    Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED), int(first.rawClass));
                    Assert::AreEqual(44, first.rawBounds.top);
                    Assert::AreEqual(316, first.rawBounds.bottom);
                    sawScopePublication |= first.decision.publish && first.decision.bounds.top == 44;
                }
                auto repeat = frame;
                repeat.cadenceRepeat = true;
                const auto repeated = diagnostic.Observe(repeat);
                Assert::IsTrue(repeated.repeated);
                Assert::IsFalse(repeated.decision.publish);
                Assert::AreEqual(unsigned(second.decision.matchingCandidates),
                    unsigned(repeated.decision.matchingCandidates));
                final = second;
            }
            Assert::IsTrue(sawFullRaster && sawScopePublication && final.decision.stable);
            Assert::AreEqual(0, final.decision.bounds.left);
            Assert::AreEqual(44, final.decision.bounds.top);
            Assert::AreEqual(640, final.decision.bounds.right);
            Assert::AreEqual(316, final.decision.bounds.bottom);
        }

        TEST_METHOD(OrderedReplayGapsAndDomainChangesRestartFreshConfirmation)
        {
            FreshReplay replay;
            auto first = SyntheticFrame(1, true);
            const auto initial = replay.Observe(first);
            Assert::IsFalse(initial.decision.stable);
            const auto gap = replay.Observe(SyntheticFrame(9, true));
            Assert::IsTrue(gap.reset);
            Assert::IsFalse(gap.decision.stable);
            Assert::AreEqual(1u, unsigned(gap.decision.matchingCandidates));
            auto changed = SyntheticFrame(10, true);
            ++changed.identity.analysisDomainKey;
            const auto domain = replay.Observe(changed);
            Assert::IsTrue(domain.reset);
            Assert::IsFalse(domain.decision.stable);
            Assert::AreEqual(1u, unsigned(domain.decision.matchingCandidates));
        }

        TEST_METHOD(OptionalColdNativeSubtitleVerticalProofDiagnostic)
        {
            wchar_t* value = nullptr;
            size_t length = 0;
            _wdupenv_s(&value, &length, L"VP_ANALYSIS_REPLAY_DIRECTORY");
            const std::filesystem::path directory(value ? value : L"");
            std::free(value);
            if (directory.empty())
            {
                Logger::WriteMessage("Cold native subtitle proof diagnostic was not requested.");
                return;
            }
            const auto frame = ReadFrame(directory / L"frame-000001.bin");
            const auto raw = ExtractActivePictureEvidence(frame.source);
            SubtitleBoxObservation key;
            key.identity = frame.identity;
            key.width = frame.source.width;
            key.height = frame.source.height;
            key.policyGeneration = frame.policy;
            key.continuityGeneration = frame.continuity;
            key.nearBarDistance = frame.nearBarDistance;
            key.optimizationMode = frame.optimizationMode;
            key.scriptHint = frame.script;
            key.discontinuity = frame.discontinuity;
            key.sharedPicture.required = true;
            key.sharedPicture.identity = frame.identity;
            SubtitleMeasurementSampler sampler;
            const auto measured = sampler.Measure(frame.source, key, {},
                10000000.0 / double(frame.frameDuration100ns), raw);
            const auto proof = EvaluateSubtitleAssistedVerticalBarProof(frame.source, raw,
                measured, frame.identity, frame.policy, frame.continuity, frame.script);
            const auto& profile = proof.evidence.verticalBarGuardProfile;
            AnalysisLumaSample mismatchSample;
            const bool sampledMismatch = profile.firstMismatchX >= 0 && profile.firstMismatchY >= 0 &&
                frame.source.Sample(profile.firstMismatchX, profile.firstMismatchY, mismatchSample);
            int inkColumn = -1, inkRow = -1, nearestSourceRow = -1;
            int ownedNeighbors = 0, rawNeighbors = 0;
            bool mismatchOwned = false, mismatchRawInk = false, mismatchRowCaptured = false;
            if (measured.ink && sampledMismatch && measured.ink->step > 0)
            {
                const auto& ink = *measured.ink;
                inkColumn = profile.firstMismatchX / ink.step;
                auto row = std::lower_bound(ink.sourceRows.begin(), ink.sourceRows.end(),
                    profile.firstMismatchY);
                if (row != ink.sourceRows.end()) inkRow = int(row - ink.sourceRows.begin());
                if (row != ink.sourceRows.begin() &&
                    (inkRow < 0 || std::abs(*(row - 1) - profile.firstMismatchY) <
                        std::abs(ink.sourceRows[size_t(inkRow)] - profile.firstMismatchY)))
                    inkRow = int(row - ink.sourceRows.begin()) - 1;
                if (inkRow >= 0)
                {
                    nearestSourceRow = ink.sourceRows[size_t(inkRow)];
                    mismatchRowCaptured = nearestSourceRow == profile.firstMismatchY;
                    mismatchOwned = ink.Get(inkColumn, inkRow, true);
                    mismatchRawInk = ink.Get(inkColumn, inkRow, false);
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            ownedNeighbors += ink.Get(inkColumn + dx, inkRow + dy, true);
                            rawNeighbors += ink.Get(inkColumn + dx, inkRow + dy, false);
                        }
                }
            }
            size_t ownedWords = 0;
            if (measured.ink)
                for (uint64_t bits : measured.ink->ownedInk)
                    ownedWords += bits != 0;
            std::ostringstream report;
            report << "cold-native-proof seq=" << frame.identity.acceptedSequence <<
                " raw_class=" << int(raw.classification) <<
                " raw=" << raw.proposedBounds.left << ',' << raw.proposedBounds.top << '-' <<
                raw.proposedBounds.right << ',' << raw.proposedBounds.bottom <<
                " h_state=" << int(raw.axisEvidence.horizontal.state) <<
                " h_reason=" << int(raw.axisEvidence.horizontal.reason) <<
                " v_reason=" << int(raw.axisEvidence.vertical.reason) <<
                " analysis_ms=" << measured.analysisMs <<
                " independent=" << measured.independentBarAnalysis <<
                " bar_authority=" << measured.barAuthority <<
                " bars=" << measured.barEvidence.available << ':' << measured.barEvidence.top << '-' <<
                measured.barEvidence.bottom <<
                " support=" << measured.barEvidence.topSupport << ',' <<
                measured.barEvidence.bottomSupport << ',' <<
                measured.barEvidence.topBoundarySupport << ',' <<
                measured.barEvidence.bottomBoundarySupport <<
                " text=" << measured.text.detected << ':' << measured.text.lineCount <<
                " anchor=" << measured.text.anchor.left << ',' << measured.text.anchor.top << '-' <<
                measured.text.anchor.right << ',' << measured.text.anchor.bottom <<
                " text_bounds=" << measured.text.bounds.left << ',' << measured.text.bounds.top << '-' <<
                    measured.text.bounds.right << ',' << measured.text.bounds.bottom <<
                " line0=" << measured.text.lineBounds[0].left << ',' << measured.text.lineBounds[0].top << '-' <<
                    measured.text.lineBounds[0].right << ',' << measured.text.lineBounds[0].bottom <<
                " source_panel=" << measured.text.sourcePanel.left << ',' << measured.text.sourcePanel.top << '-' <<
                    measured.text.sourcePanel.right << ',' << measured.text.sourcePanel.bottom <<
                " capture_panel=" << measured.text.capturePanel.left << ',' << measured.text.capturePanel.top << '-' <<
                    measured.text.capturePanel.right << ',' << measured.text.capturePanel.bottom <<
                " proven_panel=" << measured.text.provenSourcePanel.left << ',' << measured.text.provenSourcePanel.top << '-' <<
                    measured.text.provenSourcePanel.right << ',' << measured.text.provenSourcePanel.bottom <<
                " ink_step=" << (measured.ink ? measured.ink->step : 0) <<
                " owned_words=" << ownedWords <<
                " raw_ink_complete=" << (measured.ink && measured.ink->rawEvidenceComplete) <<
                " proof=" << proof.accepted << " candidate=" <<
                proof.candidate.left << ',' << proof.candidate.top << '-' <<
                proof.candidate.right << ',' << proof.candidate.bottom <<
                " reason=" << proof.reason <<
                " profile=" << proof.evidence.verticalBarGuardProfile.completed << ':' <<
                proof.evidence.verticalBarGuardProfile.clean << ':' <<
                proof.evidence.verticalBarGuardProfile.reason <<
                " aperture=" << proof.evidence.verticalBarGuardProfile.apertureTop << ':' <<
                proof.evidence.verticalBarGuardProfile.apertureBottom <<
                " first_mismatch=" << proof.evidence.verticalBarGuardProfile.firstMismatchX << ':' <<
                proof.evidence.verticalBarGuardProfile.firstMismatchY <<
                " mismatches=" << proof.evidence.verticalBarGuardProfile.mismatchSamples << ':' <<
                proof.evidence.verticalBarGuardProfile.boundaryMismatchSamples << ':' <<
                proof.evidence.verticalBarGuardProfile.deepMismatchSamples <<
                " mismatch_sample=" << sampledMismatch << ':' <<
                (sampledMismatch ? mismatchSample.luma : 0) << ':' <<
                (sampledMismatch ? mismatchSample.chromaU : 0) << ':' <<
                (sampledMismatch ? mismatchSample.chromaV : 0) <<
                " ink=" << mismatchOwned << ':' << mismatchRawInk << ':' << mismatchRowCaptured <<
                " ink_grid=" << inkColumn << ':' << inkRow << ':' << nearestSourceRow <<
                " ink_neighbors=" << ownedNeighbors << ':' << rawNeighbors <<
                " reference=" << profile.referenceY << ':' << profile.referenceU << ':' << profile.referenceV <<
                " max_delta=" << proof.evidence.verticalBarGuardProfile.maxLumaDelta << ':' <<
                proof.evidence.verticalBarGuardProfile.maxChromaDelta;
            if (measured.ink && !proof.accepted &&
                std::string(proof.reason) == "subtitle-excluded-band-profile-failed")
            {
                const auto& ink = *measured.ink;
                const auto gridRow = [&](int y) {
                    auto row = std::lower_bound(ink.sourceRows.begin(), ink.sourceRows.end(), y);
                    int nearest = row == ink.sourceRows.end() ? -1 :
                        int(row - ink.sourceRows.begin());
                    if (row != ink.sourceRows.begin() &&
                        (nearest < 0 || std::abs(*(row - 1) - y) <
                            std::abs(ink.sourceRows[size_t(nearest)] - y)))
                        nearest = int(row - ink.sourceRows.begin()) - 1;
                    return nearest;
                };
                const auto ownedNear = [&](int x, int y) {
                    const int row = gridRow(y), col = x / ink.step;
                    if (row < 0 || std::abs(ink.sourceRows[size_t(row)] - y) >
                        std::max(1, ink.step)) return false;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (ink.Get(col + dx, row + dy, true)) return true;
                    return false;
                };
                std::vector<int> rows;
                for (int d = 0; d < 8; ++d)
                {
                    rows.push_back(profile.apertureBottom + d);
                    rows.push_back(frame.source.height - 1 - d);
                }
                for (int y = profile.apertureBottom; y < frame.source.height;
                    y += std::max(1, frame.source.height / 540)) rows.push_back(y);
                std::sort(rows.begin(), rows.end());
                rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
                int mismatchIndex = 0;
                for (int y : rows)
                    for (int i = 0; i < 96; ++i)
                    {
                        const int x = i * (frame.source.width - 1) / 95;
                        AnalysisLumaSample sample;
                        if (!frame.source.Sample(x, y, sample) || ownedNear(x, y) ||
                            (std::abs(double(sample.luma) - profile.referenceY) <= 4 &&
                             std::abs(double(sample.chromaU) - profile.referenceU) <= 4 &&
                             std::abs(double(sample.chromaV) - profile.referenceV) <= 4)) continue;
                        if (++mismatchIndex > 12) continue;
                        const int col = x / ink.step, row = gridRow(y);
                        int nearestCells = INT_MAX, nearestPixels = INT_MAX;
                        for (int rr = 0; rr < ink.height; ++rr)
                        {
                            if (ink.sourceRows[size_t(rr)] < measured.text.anchor.top - ink.step ||
                                ink.sourceRows[size_t(rr)] > measured.text.anchor.bottom + ink.step) continue;
                            for (int cc = 0; cc < ink.width; ++cc)
                            {
                                if (cc * ink.step < measured.text.anchor.left - ink.step ||
                                    cc * ink.step > measured.text.anchor.right + ink.step ||
                                    !ink.Get(cc, rr, true)) continue;
                                nearestCells = std::min(nearestCells,
                                    std::max(std::abs(cc - col), std::abs(rr - row)));
                                nearestPixels = std::min(nearestPixels,
                                    std::max(std::abs(cc * ink.step - x),
                                        std::abs(ink.sourceRows[size_t(rr)] - y)));
                            }
                        }
                        bool connected = false;
                        if (row >= 0 && ink.Get(col, row, false))
                        {
                            std::vector<uint8_t> seen(size_t(ink.width) * ink.height);
                            std::deque<std::pair<int, int>> queue;
                            queue.emplace_back(col, row);
                            seen[size_t(row) * ink.width + col] = 1;
                            while (!queue.empty() && !connected)
                            {
                                const auto cell = queue.front(); queue.pop_front();
                                if (ink.Get(cell.first, cell.second, true))
                                {
                                    connected = true; break;
                                }
                                for (int dy = -1; dy <= 1; ++dy)
                                    for (int dx = -1; dx <= 1; ++dx)
                                    {
                                        const int cc = cell.first + dx, rr = cell.second + dy;
                                        if (cc < 0 || rr < 0 || cc >= ink.width || rr >= ink.height ||
                                            cc * ink.step < measured.text.anchor.left ||
                                            cc * ink.step >= measured.text.anchor.right ||
                                            ink.sourceRows[size_t(rr)] < measured.text.anchor.top ||
                                            ink.sourceRows[size_t(rr)] >= measured.text.anchor.bottom ||
                                            !ink.Get(cc, rr, false)) continue;
                                        auto& visited = seen[size_t(rr) * ink.width + cc];
                                        if (!visited) { visited = 1; queue.emplace_back(cc, rr); }
                                    }
                            }
                        }
                        std::ostringstream mismatch;
                        mismatch << "cold-profile-mismatch " << mismatchIndex <<
                            " source=" << x << ',' << y << " grid=" << col << ',' << row <<
                            " raw=" << ink.Get(col, row, false) <<
                            " owned=" << ink.Get(col, row, true) <<
                            " nearest_cells=" << nearestCells <<
                            " nearest_pixels=" << nearestPixels <<
                            " connected8=" << connected <<
                            " inside_anchor=" << (x >= measured.text.anchor.left &&
                                x < measured.text.anchor.right && y >= measured.text.anchor.top &&
                                y < measured.text.anchor.bottom) <<
                            " inside_capture=" << (x >= measured.text.capturePanel.left &&
                                x < measured.text.capturePanel.right &&
                                y >= measured.text.capturePanel.top &&
                                y < measured.text.capturePanel.bottom);
                        Logger::WriteMessage(mismatch.str().c_str());
                    }
            }            Logger::WriteMessage(report.str().c_str());
            Assert::IsTrue(proof.accepted,
                L"The cold current subtitle and native bars must prove physical scope before presentation.");
            const auto retention = EvaluateActivePicturePresentationRetention(
                frame.source, proof.candidate);
            std::ostringstream retentionReport;
            retentionReport << "cold-native-retention valid=" << retention.analysisValid
                << " presentation=" << retention.presentationValid
                << " safe=" << retention.CanRetainPresentation()
                << " bands_safe=" << retention.excludedBandsPixelSafe
                << " outward=" << retention.outwardVisibleBoundsAvailable
                << " outward_bounds=" << retention.outwardVisibleBounds.left << ','
                << retention.outwardVisibleBounds.top << '-'
                << retention.outwardVisibleBounds.right << ','
                << retention.outwardVisibleBounds.bottom
                << " observed_class=" << int(retention.activePicture.classification)
                << " observed=" << retention.activePicture.proposedBounds.left << ','
                << retention.activePicture.proposedBounds.top << '-'
                << retention.activePicture.proposedBounds.right << ','
                << retention.activePicture.proposedBounds.bottom;
            Logger::WriteMessage(retentionReport.str().c_str());
        }
        TEST_METHOD(OptionalColdNativeSubtitleOwnershipNegatives)
        {
            wchar_t* value = nullptr;
            size_t length = 0;
            _wdupenv_s(&value, &length, L"VP_ANALYSIS_REPLAY_DIRECTORY");
            const std::filesystem::path directory(value ? value : L"");
            std::free(value);
            if (directory.empty()) return;
            const auto original = ReadFrame(directory / L"frame-000001.bin");
            Assert::AreEqual(int(AnalysisLumaFormat::DecodedDolbyVision422),
                int(original.source.format));
            const auto encode = [](uint8_t* lane, int code) {
                const uint16_t packed = static_cast<uint16_t>(code << 6);
                lane[0] = static_cast<uint8_t>(packed);
                lane[1] = static_cast<uint8_t>(packed >> 8);
            };
            const auto patch = [&](int left, int top, int right, int bottom, int yCode, int uCode, int vCode) {
                ReplayFrame copy = original;
                copy.pixels = std::make_shared<std::vector<uint8_t>>(*original.pixels);
                copy.source.data = copy.pixels->data();
                copy.source.backingOwner = copy.pixels;
                const size_t chromaStart = copy.source.rowBytes * size_t(copy.source.height);
                for (int y = top; y < bottom; ++y)
                    for (int x = left; x < right; ++x)
                    {
                        encode(copy.pixels->data() + size_t(y) * copy.source.rowBytes + size_t(x) * 2,
                            yCode);
                        uint8_t* uv = copy.pixels->data() + chromaStart +
                            size_t(y) * copy.source.chromaRowBytes + size_t(x / 2) * 4;
                        encode(uv, uCode);
                        encode(uv + 2, vCode);
                    }
                return copy;
            };
            struct Case {
                const char* label;
                int left, top, right, bottom, yCode, uCode, vCode;
                bool requireRawInk;
            };
            const Case cases[] = {
                // Existing detector logic may own a glyph-like interior patch.
                // Its final bounded presentation must include the added pixels.
                {"in-seed-glyph-like-gap", 2160, 1950, 2176, 1970, 568, 512, 512, true},
                // A broader interior patch also tests the final bounded display, independent of semantic ownership.
                {"in-seed-broad-picture", 2160, 1950, 2220, 1962, 568, 512, 512, true},
                {"endpoint-solid-non-glyph", 2420, 2010, 2460, 2025, 568, 512, 512, true},
                {"endpoint-wrong-palette", 2420, 2000, 2450, 2010, 568, 700, 300, false},
                {"outside-line-picture", 2500, 1950, 2560, 1970, 568, 512, 512, true},
                {"outside-envelope-sampled-patch", 2498, 2060, 2514, 2080, 568, 512, 512, true},
                {"outside-envelope-between-columns", 2520, 2060, 2536, 2080, 568, 512, 512, false},
            };
            bool allSafe = true;
            for (const auto& item : cases)
            {
                AnalysisLumaSample originalPixel;
                Assert::IsTrue(original.source.Sample(item.left, item.top, originalPixel));
                const bool originalPixelWasBlack = originalPixel.luma <= 24;
                const auto frame = patch(item.left, item.top, item.right, item.bottom,
                    item.yCode, item.uCode, item.vCode);
                const auto raw = ExtractActivePictureEvidence(frame.source);
                SubtitleBoxObservation key;
                key.identity = frame.identity;
                key.width = frame.source.width;
                key.height = frame.source.height;
                key.policyGeneration = frame.policy;
                key.continuityGeneration = frame.continuity;
                key.nearBarDistance = frame.nearBarDistance;
                key.optimizationMode = frame.optimizationMode;
                key.scriptHint = frame.script;
                key.sharedPicture.required = true;
                key.sharedPicture.identity = frame.identity;
                SubtitleMeasurementSampler sampler;
                const auto measured = sampler.Measure(frame.source, key, {},
                    10000000.0 / double(frame.frameDuration100ns), raw);
                const auto proof = EvaluateSubtitleAssistedVerticalBarProof(frame.source, raw,
                    measured, frame.identity, frame.policy, frame.continuity, frame.script);
                int ownedCells = 0, rawCells = 0;
                if (measured.ink && measured.ink->step > 0)
                {
                    const auto& ink = *measured.ink;
                    for (int row = 0; row < ink.height; ++row)
                    {
                        const int sourceY = ink.sourceRows[size_t(row)];
                        if (sourceY < item.top || sourceY >= item.bottom) continue;
                        for (int col = item.left / ink.step;
                            col <= item.right / ink.step && col < ink.width; ++col)
                        {
                            ownedCells += ink.Get(col, row, true);
                            rawCells += ink.Get(col, row, false);
                        }
                    }
                }
                AlphaSourceCrop::ColdMaskedCaptionPresentationDecision envelope;
                if (proof.accepted && measured.text.bounds.Valid())
                {
                    const auto retention = EvaluateActivePicturePresentationRetention(
                        frame.source, proof.candidate);
                    AlphaSourceCrop::Input crop;
                    crop.automaticCropEnabled = true;
                    crop.nearBlackEpisodeFullRaster = true;
                    crop.sharedGeometryAvailable = true;
                    crop.latestObservationSupportsCrop = true;
                    crop.latestObservationClassification =
                        ActivePictureClassification::BAR_CROP_TRUSTED;
                    crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
                    crop.geometry = proof.candidate;
                    crop.geometrySourceGeneration = crop.frameSourceGeneration =
                        frame.identity.transportGeneration;
                    crop.frameSourceSequence = frame.identity.acceptedSequence;
                    crop.framePresentationEpoch = frame.identity.viewportGeneration;
                    crop.rasterWidth = frame.source.width;
                    crop.rasterHeight = frame.source.height;
                    crop.currentVisibleBoundsAvailable =
                        retention.analysisValid && retention.presentationValid &&
                        retention.outwardVisibleBoundsAvailable;
                    crop.currentVisibleBase = proof.candidate;
                    crop.currentVisibleBounds = retention.outwardVisibleBounds;
                    crop.currentVisibleSourceGeneration = frame.identity.transportGeneration;
                    crop.currentVisibleSourceSequence = frame.identity.acceptedSequence;
                    AlphaSourceCrop::ColdMaskedCaptionPresentationInput cold;
                    cold.crop = &crop;
                    cold.episode.mode = AlphaSourceCrop::NearBlackPresentationMode::FULL_RASTER;
                    cold.episode.sourceGeneration = frame.identity.transportGeneration;
                    cold.episode.presentationEpoch = frame.identity.viewportGeneration;
                    cold.strictProofCurrent = true;
                    cold.strictPicture = proof.candidate;
                    cold.proofSourceGeneration = frame.identity.transportGeneration;
                    cold.proofSourceSequence = frame.identity.acceptedSequence;
                    cold.proofPresentationEpoch = frame.identity.viewportGeneration;
                    // Repeated equivalent source samples have confirmed this
                    // measured cue; this probe isolates final pixel containment.
                    cold.captionConfirmed = true;
                    cold.captionBounds = { measured.text.bounds.left,
                        measured.text.bounds.top, measured.text.bounds.right,
                        measured.text.bounds.bottom, frame.source.width,
                        frame.source.height, 1.0, ActivePictureBounds::BarAxes::NONE };
                    cold.captionSourceGeneration = frame.identity.transportGeneration;
                    cold.captionSourceSequence = frame.identity.acceptedSequence;
                    cold.currentObservationAvailable = retention.activePicture.available;
                    cold.currentObservation = retention.activePicture.proposedBounds;
                    cold.observationSourceGeneration = frame.identity.transportGeneration;
                    cold.observationSourceSequence = frame.identity.acceptedSequence;
                    cold.currentRetentionEvaluated =
                        retention.analysisValid && retention.presentationValid;
                    cold.retentionSourceGeneration = frame.identity.transportGeneration;
                    cold.retentionSourceSequence = frame.identity.acceptedSequence;
                    cold.retentionBase = proof.candidate;
                    envelope = AlphaSourceCrop::BuildColdMaskedCaptionPresentation(cold);
                }
                const bool patchVisible = !envelope.available ||
                    (envelope.presentation.sourceBounds.left <= item.left &&
                     envelope.presentation.sourceBounds.top <= item.top &&
                     envelope.presentation.sourceBounds.right >= item.right &&
                     envelope.presentation.sourceBounds.bottom >= item.bottom);
                std::ostringstream report;
                report << "cold-negative=" << item.label << " original_y=" << originalPixel.luma << " original_black=" << originalPixelWasBlack << " raw_class=" <<
                    int(raw.classification) << " text=" << measured.text.detected <<
                    " lines=" << measured.text.lineCount << " raw_cells=" << rawCells <<
                    " owned_cells=" << ownedCells << " raw_bounds=" << raw.proposedBounds.left << "," << raw.proposedBounds.top << "," << raw.proposedBounds.right << "," << raw.proposedBounds.bottom << " caption_bounds=" << measured.text.bounds.left << "," << measured.text.bounds.top << "," << measured.text.bounds.right << "," << measured.text.bounds.bottom << " proof=" << proof.accepted <<
                    " reason=" << proof.reason << " proposal=" << envelope.available <<
                    " proposal_bottom=" << envelope.presentation.sourceBounds.bottom <<
                    " patch_visible=" << patchVisible;
                Logger::WriteMessage(report.str().c_str());
                allSafe = allSafe && (!item.requireRawInk || rawCells > 0) && patchVisible;
            }
            Assert::IsTrue(allSafe,
                L"Every admitted crop must contain all current adversarial pixels, including caption-like ink.");
        }
        TEST_METHOD(OptionalNativeBlackReferenceCaptureReleasesSubmittedScope)
        {
            wchar_t* value = nullptr;
            size_t length = 0;
            _wdupenv_s(&value, &length, L"VP_BLACK_REFERENCE_REPLAY_DIRECTORY");
            const std::filesystem::path directory(value ? value : L"");
            std::free(value);
            if (directory.empty())
            {
                Logger::WriteMessage("Native black-reference regression capture was not requested.");
                return;
            }
            std::vector<std::filesystem::path> paths;
            for (const auto& entry : std::filesystem::directory_iterator(directory))
                if (entry.is_regular_file() && entry.path().extension() == L".bin" &&
                    entry.path().filename().wstring().rfind(L"frame-", 0) == 0)
                    paths.push_back(entry.path());
            std::sort(paths.begin(), paths.end());
            Assert::IsTrue(!paths.empty() && paths.size() <= 4096);
            const auto first = ReadFrame(paths.front());
            Assert::IsTrue(first.identity.acceptedSequence > 16);
            Assert::IsTrue(first.source.range == AnalysisLumaRange::Full &&
                first.source.format == AnalysisLumaFormat::DecodedDolbyVision422 &&
                first.frameDuration100ns != 0);
            int top = 0, bottom = 0;
            {
                auto sidecar = paths.front(); sidecar.replace_extension(L".txt");
                std::ifstream metadata(sidecar);
                int width = 0, height = 0, format = 0, encoding = 0, colorspace = 0;
                size_t stride = 0, chromaStride = 0;
                metadata >> width >> height >> stride >> chromaStride >> format >> encoding >>
                    colorspace >> top >> bottom;
                Assert::IsTrue(bool(metadata) && top > 0 && bottom > top && bottom < height &&
                    (top & 1) == 0 && (bottom & 1) == 0);
            }
            // A derived adversary removes chroma while retaining captured luma.
            // It is a measurement-only fixture, never a rewritten captured observation.
            auto neutralPixels = std::make_shared<std::vector<uint8_t>>(*first.pixels);
            auto neutralSource = first.source;
            neutralSource.data = neutralPixels->data(); neutralSource.backingOwner = neutralPixels;
            const size_t chromaOffset = neutralSource.rowBytes*size_t(neutralSource.height);
            for (size_t offset = chromaOffset; offset+1 < neutralPixels->size(); offset += 2)
            {
                (*neutralPixels)[offset] = 0; (*neutralPixels)[offset+1] = 128; // 512<<6 in LE16.
            }
            const auto neutralEvidence = ExtractActivePictureEvidence(neutralSource);
            Assert::IsTrue(neutralEvidence.classification == ActivePictureClassification::FULL_RASTER_TRUSTED,
                L"The correction must follow full-band luma evidence, not a scene-specific chroma code.");
            const int width = first.source.width, height = first.source.height;
            const ActivePictureBounds raster = {0,0,width,height,width,height,
                double(width)/height,ActivePictureBounds::BarAxes::NONE};
            const ActivePictureBounds scope = {0,top,width,bottom,width,height,
                double(width)/(bottom-top),ActivePictureBounds::BarAxes::TOP_BOTTOM};
            // Explicit synthetic prehistory acquires the sidecar's retained Scope.
            // Captured files keep their actual identities, repeats and omissions.
            auto seedPixels = std::make_shared<std::vector<uint8_t>>(size_t(width)*height*4);
            auto* words = reinterpret_cast<uint16_t*>(seedPixels->data());
            std::fill(words + size_t(width)*height, words + size_t(width)*height*2,
                uint16_t(512 << 6));
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    words[size_t(y)*width+x] = uint16_t((y < top || y >= bottom ? 0 :
                        240 + ((x/32+y/32)%7)*24) << 6);
            auto seedSource = first.source;
            seedSource.format = AnalysisLumaFormat::DecodedDolbyVision422;
            seedSource.rowBytes = seedSource.chromaRowBytes = size_t(width)*2;
            seedSource.data = seedPixels->data(); seedSource.dataBytes = seedPixels->size();
            seedSource.backingOwner = seedPixels;
            const auto seedEvidence = ExtractActivePictureEvidence(seedSource);
            Assert::IsTrue(seedEvidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::AreEqual(top,seedEvidence.trustedBounds.top);
            Assert::AreEqual(bottom,seedEvidence.trustedBounds.bottom);
            ActivePictureTransitionModel model;
            ActivePictureTransitionDecision transition;
            for (uint64_t sequence = first.identity.acceptedSequence-8;
                sequence < first.identity.acceptedSequence; ++sequence)
                transition = model.Observe(MakeActivePictureObservation(seedEvidence,sequence,
                    10000000.0/double(first.frameDuration100ns)));
            Assert::IsTrue(transition.stable);
            AlphaSourceCrop::Input crop;
            crop.automaticCropEnabled = crop.sharedGeometryAvailable = true;
            crop.latestObservationSupportsCrop = true;
            crop.classification = crop.latestObservationClassification =
                ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.geometry = scope;
            crop.rasterWidth = width; crop.rasterHeight = height;
            crop.frameSourceGeneration = crop.geometrySourceGeneration = first.identity.transportGeneration;
            crop.frameSourceSequence = first.identity.acceptedSequence-1;
            crop.framePresentationEpoch = first.identity.viewportGeneration;
            auto admission = AlphaSourceCrop::AdmitCropPresentation({},crop,
                AlphaSourceCrop::Evaluate(crop),first.identity.viewportGeneration).state;
            Assert::IsTrue(admission.available);
            AlphaSourceCrop::SubmittedNativeScopeAnchorUpdateInput seedCommit;
            seedCommit.selectedAdmission = admission;
            seedCommit.nativeCropAvailable = true; seedCommit.nativeCrop = scope;
            seedCommit.submittedBounds = scope;
            seedCommit.selectedOwner = AlphaSourceCrop::DecisionOwner::TRUSTED_CROP;
            seedCommit.sourceGeneration = first.identity.transportGeneration;
            seedCommit.sourceSequence = first.identity.acceptedSequence-1;
            seedCommit.presentationEpoch = first.identity.viewportGeneration;
            seedCommit.frameSubmitted = true;
            auto anchor = AlphaSourceCrop::CommitSubmittedNativeScopeAnchor(seedCommit);
            Assert::IsTrue(anchor.available);
            AlphaSourceCrop::PresentationRecoveryState recovery;
            recovery.active = true; recovery.trustedCrop = scope;
            recovery.sourceGeneration = first.identity.transportGeneration;
            recovery.presentationEpoch = first.identity.viewportGeneration;
            recovery.startedSourceSequence = first.identity.acceptedSequence-1;
            recovery.lastSourceSequence = first.identity.acceptedSequence-1;
            recovery.startedTick = 1;
            AlphaSourceCrop::OutwardPictureConfirmationState outward;
            auto logical = scope;
            auto logicalClass = ActivePictureClassification::BAR_CROP_TRUSTED;
            ReplayFrame previous;
            bool hasPrevious = false, fullSubmitted = false, fullAuthority = false;
            uint64_t firstFullSequence = 0, firstLogicalFullSequence = 0;
            unsigned unique = 0, repeats = 0, gaps = 0;
            double elapsedMs = 0.0;
            for (const auto& path : paths)
            {
                const auto frame = ReadFrame(path);
                Assert::IsTrue(SameReplayContext(first,frame) && !frame.discontinuity,
                    L"This retained-history replay requires one exact captured context.");
                if (hasPrevious && SameAnalysisFrameIdentity(frame.identity,previous.identity))
                {
                    ++repeats;
                    continue; // A display repeat never contributes another observation.
                }
                const bool gap = hasPrevious && frame.identity.acceptedSequence != previous.identity.acceptedSequence+1;
                if (gap) ++gaps;
                const auto begin = std::chrono::steady_clock::now();
                const auto raw = ExtractActivePictureEvidence(frame.source);
                elapsedMs += std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-begin).count();
                Assert::IsTrue(raw.classification == ActivePictureClassification::FULL_RASTER_TRUSTED,
                    L"The captured dark wall must not nominate an inward left crop.");
                Assert::IsTrue(raw.axisEvidence.horizontal.state == ActivePictureAxisState::FULL_EXTENT_SUPPORTED &&
                    raw.axisEvidence.vertical.state == ActivePictureAxisState::FULL_EXTENT_SUPPORTED,
                    L"Nominal classification must have affirmative current support on all raster edges.");
                auto retention = EvaluateActivePicturePresentationRetention(frame.source,logical);
                AlphaSourceCrop::TransitionAdmissionInput change;
                change.evidence = raw;
                change.outwardCandidate = raw.trustedBounds;
                change.presentationBeforeObservation = logical;
                change.trustedGeometry = logical; change.trustedGeometryAvailable = true;
                change.compatiblePresentation = logicalClass == ActivePictureClassification::BAR_CROP_TRUSTED;
                change.trustedGeneration = change.sourceGeneration = frame.identity.transportGeneration;
                change.sourceSequence = frame.identity.acceptedSequence;
                change.framesPerSecond = 10000000.0/double(frame.frameDuration100ns);
                change.retention = retention; change.previousOutward = outward;
                const auto changed = AlphaSourceCrop::EvaluateTransitionAdmission(change);
                if (gap && change.compatiblePresentation)
                    Assert::AreEqual(1u,unsigned(changed.outward.state.confirmations));
                outward = changed.outward.state;
                transition = model.Observe(changed.observation);
                if (transition.publish)
                {
                    Assert::IsTrue(changed.outward.authoritative ||
                        logicalClass == ActivePictureClassification::FULL_RASTER_TRUSTED,
                        L"The old Scope cannot publish a replacement before current outward proof.");
                    logical = transition.bounds; logicalClass = raw.classification;
                    if (!firstLogicalFullSequence && logicalClass == ActivePictureClassification::FULL_RASTER_TRUSTED)
                        firstLogicalFullSequence = frame.identity.acceptedSequence;
                    retention = EvaluateActivePicturePresentationRetention(frame.source,logical);
                }
                crop.geometry = logical; crop.classification = logicalClass;
                crop.frameSourceSequence = frame.identity.acceptedSequence;
                crop.latestObservationSupportsCrop = false;
                crop.latestObservationClassification = raw.classification;
                fullAuthority = AlphaSourceCrop::UpdateFullRasterPresentationAuthority(fullAuthority,
                    raw.classification,raw.trustedBounds.left == 0 && raw.trustedBounds.top == 0 &&
                    raw.trustedBounds.right == width && raw.trustedBounds.bottom == height,raw.authorityOrigin);
                crop.fullRasterPresentationAuthoritative = fullAuthority;
                crop.frameLocalPresentationRetentionEvaluated = true;
                crop.frameLocalPresentationRetentionSafe = retention.excludedBandsPixelSafe;
                crop.currentVisibleBoundsAvailable = retention.outwardVisibleBoundsAvailable;
                crop.currentVisibleBounds = retention.outwardVisibleBounds;
                crop.currentVisibleBase = logical;
                crop.currentVisibleSourceGeneration = frame.identity.transportGeneration;
                crop.currentVisibleSourceSequence = frame.identity.acceptedSequence;
                AlphaSourceCrop::PresentationRecoveryInput input;
                input.previous = recovery; input.previousAdmission = admission;
                input.submittedScopeAnchor = anchor; input.crop = crop;
                input.measurementCurrent = input.retentionEvaluated = input.nearBlackEvaluated = true;
                input.excludedBandsPixelSafe = retention.excludedBandsPixelSafe;
                input.observationAvailable = retention.proposedBoundsAvailable;
                input.observation = retention.activePicture.proposedBounds;
                input.observedTrustedCrop = retention.activePicture.trustedBounds;
                input.observationClassification = retention.activePicture.classification;
                input.retentionBounds = logical;
                input.retentionSourceGeneration = frame.identity.transportGeneration;
                input.retentionSourceSequence = frame.identity.acceptedSequence;
                input.globalNearBlack = retention.EffectiveArNearBlack();
                input.presentationEpoch = frame.identity.viewportGeneration;
                input.currentTick = 1000 + (frame.identity.acceptedSequence-first.identity.acceptedSequence)*
                    frame.frameDuration100ns/10000;
                input.framesPerSecond = change.framesPerSecond;
                input.candidate = AlphaSourceCrop::Evaluate(crop);
                const auto recovered = AlphaSourceCrop::EvaluatePresentationRecovery(input);
                recovery = recovered.state; input.candidate = recovered.presentation;
                const auto bounded = AlphaSourceCrop::EvaluateNearBlackBoundedPresentation({},input);
                const auto admitted = AlphaSourceCrop::AdmitCropPresentation(admission,crop,
                    bounded.presentation,input.presentationEpoch);
                admission = admitted.state;
                const auto uncertainty = AlphaSourceCrop::ResolveUncertainPicturePresentation(input,admitted.presentation);
                if (uncertainty.retained)
                {
                    crop.geometry = uncertainty.nativeCrop;
                    crop.geometrySourceGeneration = crop.frameSourceGeneration;
                    crop.sharedGeometryAvailable = true;
                    crop.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
                    crop.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
                    crop.latestObservationSupportsCrop = false;
                    admission.available = true; admission.trustedCrop = uncertainty.nativeCrop;
                    admission.sourceGeneration = crop.frameSourceGeneration;
                    admission.presentationEpoch = input.presentationEpoch;
                    admission.authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
                    admission.outwardPresentationAvailable = uncertainty.expanded;
                    admission.outwardPresentation = uncertainty.presentation.sourceBounds;
                    admission.presentationSourceSequence = crop.frameSourceSequence;
                }
                AlphaSourceCrop::FinalAspectLimitFillInput fill;
                fill.crop = crop; fill.evidence = input;
                fill.presentation = uncertainty.presentation; fill.uncertainPicture = uncertainty;
                fill.recoveryBoundedPresentation = recovered.boundedPresentation;
                fill.configuredScreenActive = true; fill.configured.screenAspect = 2.35;
                fill.configured.cropNarrowerContentToFillScreen = fill.configured.narrowerLimitConfigured = true;
                fill.configured.narrowerAspectLimit = 2.32;
                fill.configured.cropWiderContentToFillScreen = fill.configured.widerLimitConfigured = true;
                fill.configured.widerAspectLimit = 2.42;
                const auto selected = AlphaSourceCrop::EvaluateFinalAspectLimitFill(fill);
                if (crop.fullRasterPresentationAuthoritative)
                {
                    Assert::IsFalse(uncertainty.retained || recovered.state.active || admitted.blocked || selected.applied);
                    Assert::AreEqual(0,selected.sourceBounds.left); Assert::AreEqual(0,selected.sourceBounds.top);
                    Assert::AreEqual(width,selected.sourceBounds.right); Assert::AreEqual(height,selected.sourceBounds.bottom);
                    AlphaSourceCrop::SubmittedNativeScopeAnchorUpdateInput commit;
                    commit.previous = anchor; commit.fullRasterAuthority = true;
                    commit.sourceGeneration = frame.identity.transportGeneration;
                    commit.sourceSequence = frame.identity.acceptedSequence;
                    commit.presentationEpoch = frame.identity.viewportGeneration;
                    commit.submittedBounds = raster;
                    const auto failed = AlphaSourceCrop::CommitSubmittedNativeScopeAnchor(commit);
                    Assert::AreEqual(anchor.available,failed.available);
                    Assert::AreEqual(anchor.sourceGeneration,failed.sourceGeneration);
                    Assert::AreEqual(anchor.presentationEpoch,failed.presentationEpoch);
                    Assert::AreEqual(anchor.lastSubmittedSourceSequence,failed.lastSubmittedSourceSequence);
                    Assert::AreEqual(anchor.lastSubmittedPresentationSourceSequence,failed.lastSubmittedPresentationSourceSequence);
                    Assert::AreEqual(anchor.crop.top,failed.crop.top); Assert::AreEqual(anchor.crop.bottom,failed.crop.bottom);
                    commit.frameSubmitted = true;
                    anchor = AlphaSourceCrop::CommitSubmittedNativeScopeAnchor(commit);
                    Assert::IsFalse(anchor.available);
                    if (!fullSubmitted) firstFullSequence = frame.identity.acceptedSequence;
                    fullSubmitted = true;
                }
                previous = frame; previous.pixels.reset(); previous.source.backingOwner.reset();
                previous.source.data = nullptr; hasPrevious = true; ++unique;
            }
            Assert::IsTrue(fullSubmitted && unique >= 5 && firstLogicalFullSequence != 0);
            Assert::AreEqual(first.identity.acceptedSequence,firstFullSequence,
                L"Current affirmative native full raster must release presentation on its first eligible source.");
            std::ostringstream report;
            report << "native-black-reference sequence_files=" << paths.size() << " unique=" << unique <<
                " repeats=" << repeats << " gaps=" << gaps << " first_full_submitted_sequence=" << firstFullSequence <<
                " first_logical_full_sequence=" << firstLogicalFullSequence << " clock=source-duration" <<
                " raw_mean_ms=" << elapsedMs/unique << " source_pixels=original identity_rewrite=none";
            Logger::WriteMessage(report.str().c_str());
        }

        TEST_METHOD(OptionalOrderedNativeAnalysisCaptureReportsFreshAcquisition)
        {
            wchar_t* value = nullptr;
            size_t length = 0;
            _wdupenv_s(&value, &length, L"VP_ANALYSIS_REPLAY_DIRECTORY");
            const std::filesystem::path directory(value ? value : L"");
            std::free(value);
            if (directory.empty())
            {
                Logger::WriteMessage("Ordered native replay was not requested.");
                return;
            }
            Assert::IsTrue(std::filesystem::is_directory(directory));
            std::vector<std::filesystem::path> frames;
            for (const auto& entry : std::filesystem::directory_iterator(directory))
                if (entry.is_regular_file() && entry.path().extension() == L".bin" &&
                    entry.path().filename().wstring().rfind(L"frame-", 0) == 0)
                    frames.push_back(entry.path());
            Assert::IsTrue(!frames.empty() && frames.size() <= 4096,
                L"Capture must contain a bounded ordered frame-*.bin sequence.");
            std::sort(frames.begin(), frames.end());
            Logger::WriteMessage("Fresh production-component replay: gaps reset confirmation. Compare these identities with live JSONL decisions; prior renderer/history/presentation/worker timing is not reconstructed.");

            wchar_t* expectedValue = nullptr;
            size_t expectedLength = 0;
            _wdupenv_s(&expectedValue, &expectedLength, L"VP_ANALYSIS_REPLAY_EXPECTED_CROP");
            const std::wstring expectedText(expectedValue ? expectedValue : L"");
            std::free(expectedValue);
            const bool assertExpectedCrop = !expectedText.empty();
            int expectedLeft = 0, expectedTop = 0, expectedRight = 0, expectedBottom = 0;
            if (assertExpectedCrop)
            {
                std::wistringstream parser(expectedText);
                wchar_t a = 0, b = 0, c = 0;
                parser >> expectedLeft >> a >> expectedTop >> b >>
                    expectedRight >> c >> expectedBottom;
                const bool parsed = bool(parser);
                wchar_t extra = 0;
                const bool hasExtra = bool(parser >> extra);
                Assert::IsTrue(parsed && !hasExtra &&
                    a == L',' && b == L',' && c == L',' &&
                    expectedLeft >= 0 && expectedTop >= 0 &&
                    expectedRight > expectedLeft && expectedBottom > expectedTop,
                    L"Expected crop must be left,top,right,bottom.");
            }
            wchar_t* envelopeValue = nullptr;
            size_t envelopeLength = 0;
            _wdupenv_s(&envelopeValue, &envelopeLength,
                L"VP_ANALYSIS_REPLAY_EXPECTED_CAPTION_BOTTOM");
            const std::wstring envelopeText(envelopeValue ? envelopeValue : L"");
            std::free(envelopeValue);
            const bool assertExpectedEnvelope = !envelopeText.empty();
            int expectedEnvelopeBottom = 0;
            if (assertExpectedEnvelope)
            {
                std::wistringstream parser(envelopeText);
                parser >> expectedEnvelopeBottom;
                const bool parsed = bool(parser);
                wchar_t extra = 0;
                Assert::IsTrue(parsed && !bool(parser >> extra) &&
                    expectedEnvelopeBottom > expectedBottom,
                    L"Expected caption bottom must be a single integer below the physical picture edge.");
            }
            wchar_t* arOnlyValue = nullptr;
            size_t arOnlyLength = 0;
            _wdupenv_s(&arOnlyValue, &arOnlyLength,
                L"VP_ANALYSIS_REPLAY_REQUIRE_AR_ONLY");
            const std::wstring arOnlyText(arOnlyValue ? arOnlyValue : L"");
            std::free(arOnlyValue);
            Assert::IsTrue(arOnlyText.empty() || arOnlyText == L"1",
                L"AR-only replay switch must be unset or 1.");
            const bool requireArOnly = arOnlyText == L"1";
            bool admittedExpectedEnvelope = false;
            bool publishedExpectedCrop = false;
            bool admittedExpectedCrop = false;
            bool benchmarkedNativeExtraction = false;
            AlphaSourceCrop::CropPresentationAdmissionState admission;
            FreshReplay replay(requireArOnly);
            for (const auto& path : frames)
            {
                const auto frame = ReadFrame(path);
                if (assertExpectedCrop && !benchmarkedNativeExtraction)
                {
                    constexpr int trials = 5;
                    double totalMs = 0.0, peakMs = 0.0;
                    size_t lumaSamples = 0, chromaSamples = 0;
                    for (int trial = 0; trial < trials; ++trial)
                    {
                        const auto begin = std::chrono::steady_clock::now();
                        const auto evidence = ExtractActivePictureEvidence(frame.source);
                        const double elapsedMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - begin).count();
                        totalMs += elapsedMs;
                        peakMs = std::max(peakMs, elapsedMs);
                        lumaSamples = evidence.lumaSamples;
                        chromaSamples = evidence.chromaSamples;
                    }
                    std::ostringstream timing;
                    timing << "Native cached-frame extraction: sequence="
                        << frame.identity.acceptedSequence << " trials=" << trials
                        << " mean_ms=" << totalMs / trials << " peak_ms=" << peakMs
                        << " luma_samples=" << lumaSamples
                        << " chroma_samples=" << chromaSamples;
                    Logger::WriteMessage(timing.str().c_str());
                    benchmarkedNativeExtraction = true;
                }
                const auto result = replay.Observe(frame);
                Logger::WriteMessage(Describe(result).c_str());
                if (assertExpectedCrop && !result.repeated)
                {
                    Assert::IsTrue(expectedRight <= frame.source.width &&
                        expectedBottom <= frame.source.height);
                    Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),
                        int(result.logicalClass), L"Captured sample lacks current native or subtitle-assisted crop authority.");
                    Assert::AreEqual(expectedLeft, result.logicalBounds.left);
                    Assert::AreEqual(expectedTop, result.logicalBounds.top);
                    Assert::AreEqual(expectedRight, result.logicalBounds.right);
                    Assert::AreEqual(expectedBottom, result.logicalBounds.bottom);
                    if (result.reset) admission = {};
                    if (result.decision.publish && result.decision.stable)
                    {
                        publishedExpectedCrop = true;
                        Assert::AreEqual(expectedLeft, result.decision.bounds.left);
                        Assert::AreEqual(expectedTop, result.decision.bounds.top);
                        Assert::AreEqual(expectedRight, result.decision.bounds.right);
                        Assert::AreEqual(expectedBottom, result.decision.bounds.bottom);
                        Assert::IsTrue((unsigned(result.decision.bounds.trustedBarAxes) &
                            unsigned(ActivePictureBounds::BarAxes::TOP_BOTTOM)) != 0,
                            L"Newly published bounds must carry vertical crop authority.");
                        AlphaSourceCrop::Input crop;
                        crop.automaticCropEnabled = true;
                        crop.sharedGeometryAvailable = true;
                        crop.latestObservationSupportsCrop = true;
                        crop.latestObservationClassification = result.logicalClass;
                        crop.classification = result.decision.authoritativeClassification;
                        crop.geometry = result.decision.bounds;
                        crop.geometrySourceGeneration = crop.frameSourceGeneration =
                            frame.identity.transportGeneration;
                        crop.frameSourceSequence = frame.identity.acceptedSequence;
                        crop.rasterWidth = frame.source.width;
                        crop.rasterHeight = frame.source.height;
                        const auto selected = AlphaSourceCrop::Evaluate(crop);

                        const auto accepted = AlphaSourceCrop::AdmitCropPresentation(
                            admission, crop, selected,
                            frame.identity.viewportGeneration);
                        std::ostringstream admissionTrace;
                        admissionTrace << "Native expected admission sequence=" << result.sequence
                            << " selected=" << selected.applyCrop << "/" << int(selected.owner)
                            << " bounds=" << selected.sourceBounds.left << "," << selected.sourceBounds.top
                            << "-" << selected.sourceBounds.right << "," << selected.sourceBounds.bottom
                            << " reason=" << selected.reason << " accepted=" << accepted.presentation.applyCrop
                            << " blocked=" << accepted.blocked << " bounds="
                            << accepted.presentation.sourceBounds.left << "," << accepted.presentation.sourceBounds.top
                            << "-" << accepted.presentation.sourceBounds.right << "," << accepted.presentation.sourceBounds.bottom
                            << " reason=" << accepted.presentation.reason;
                        Logger::WriteMessage(admissionTrace.str().c_str());
                        Assert::IsFalse(accepted.blocked);
                        Assert::IsTrue(accepted.presentation.applyCrop);
                        Assert::AreEqual(expectedLeft, accepted.presentation.sourceBounds.left);
                        Assert::AreEqual(expectedTop, accepted.presentation.sourceBounds.top);
                        Assert::AreEqual(expectedRight, accepted.presentation.sourceBounds.right);
                        Assert::AreEqual(expectedBottom, accepted.presentation.sourceBounds.bottom);
                        if (assertExpectedEnvelope && result.subtitleProof &&
                            result.captionConfirmed)
                        {
                            const auto retention = EvaluateActivePicturePresentationRetention(
                                frame.source, result.decision.bounds);
                            crop.nearBlackEpisodeFullRaster = true;
                            crop.framePresentationEpoch = frame.identity.viewportGeneration;
                            crop.currentVisibleBoundsAvailable =
                                retention.analysisValid && retention.presentationValid &&
                                retention.outwardVisibleBoundsAvailable;
                            crop.currentVisibleBase = result.decision.bounds;
                            crop.currentVisibleSourceGeneration = frame.identity.transportGeneration;
                            crop.currentVisibleSourceSequence = frame.identity.acceptedSequence;
                            crop.currentVisibleBounds = retention.outwardVisibleBounds;
                            if (crop.currentVisibleBoundsAvailable)
                            {
                                auto& visible = crop.currentVisibleBounds;
                                visible.left = std::min(visible.left, result.decision.bounds.left);
                                visible.top = std::min(visible.top, result.decision.bounds.top);
                                visible.right = std::max(visible.right, result.decision.bounds.right);
                                visible.bottom = std::max(visible.bottom, result.decision.bounds.bottom);
                                visible.aspectRatio = double(visible.right - visible.left) /
                                    std::max(1, visible.bottom - visible.top);
                            }
                            AlphaSourceCrop::ColdMaskedCaptionPresentationInput cold;
                            cold.crop = &crop;
                            cold.episode.mode = AlphaSourceCrop::NearBlackPresentationMode::FULL_RASTER;
                            cold.episode.sourceGeneration = frame.identity.transportGeneration;
                            cold.episode.presentationEpoch = frame.identity.viewportGeneration;
                            cold.previousAdmission = admission;
                            cold.strictProofCurrent = result.subtitleProof;
                            cold.strictPicture = result.decision.bounds;
                            cold.proofSourceGeneration = frame.identity.transportGeneration;
                            cold.proofSourceSequence = frame.identity.acceptedSequence;
                            cold.proofPresentationEpoch = frame.identity.viewportGeneration;
                            cold.captionConfirmed = result.captionConfirmed;
                            cold.captionBounds = result.captionBounds;
                            cold.captionSourceGeneration = frame.identity.transportGeneration;
                            cold.captionSourceSequence = frame.identity.acceptedSequence;
                            cold.currentObservationAvailable = retention.activePicture.available;
                            cold.currentObservation = retention.activePicture.proposedBounds;
                            cold.observationSourceGeneration = frame.identity.transportGeneration;
                            cold.observationSourceSequence = frame.identity.acceptedSequence;
                            cold.currentRetentionEvaluated =
                                retention.analysisValid && retention.presentationValid;
                            cold.currentRetentionSafe = retention.CanRetainPresentation();
                            cold.currentExcludedBandsPixelSafe = retention.excludedBandsPixelSafe;
                            cold.currentOutwardVisible = retention.outwardVisibleBoundsAvailable;
                            cold.currentNativeTrusted =
                                retention.activePicture.classification ==
                                    ActivePictureClassification::BAR_CROP_TRUSTED &&
                                retention.activePicture.authorityOrigin ==
                                    ActivePictureAuthorityOrigin::NATIVE;
                            cold.currentNativeBounds = retention.activePicture.trustedBounds;
                            cold.captionMeasured = result.captionConfirmed;
                            cold.retentionSourceGeneration = frame.identity.transportGeneration;
                            cold.retentionSourceSequence = frame.identity.acceptedSequence;
                            cold.retentionBase = result.decision.bounds;
                            const auto envelope =
                                AlphaSourceCrop::BuildColdMaskedCaptionPresentation(cold);
                            std::ostringstream envelopeTrace;
                            envelopeTrace << "Native caption envelope sequence=" << result.sequence
                                << " caption_obs=" << result.captionObservations
                                << " caption=" << result.captionBounds.left << ','
                                << result.captionBounds.top << '-'
                                << result.captionBounds.right << ','
                                << result.captionBounds.bottom
                                << " retention=" << retention.analysisValid << '/'
                                << retention.presentationValid << '/'
                                << retention.outwardVisibleBoundsAvailable
                                << " visible=" << crop.currentVisibleBounds.left << ','
                                << crop.currentVisibleBounds.top << '-'
                                << crop.currentVisibleBounds.right << ','
                                << crop.currentVisibleBounds.bottom
                                << " available=" << envelope.available
                                << " reason=" << envelope.reason;
                            Logger::WriteMessage(envelopeTrace.str().c_str());
                            Assert::IsTrue(envelope.available,
                                L"Current native bars, confirmed caption, and current visible pixels must form a bounded envelope.");
                            const auto final = AlphaSourceCrop::AdmitCropPresentation(
                                admission, crop, envelope.presentation,
                                frame.identity.viewportGeneration);
                            Assert::IsFalse(final.blocked);
                            Assert::IsTrue(final.presentation.applyCrop);
                            Assert::AreEqual(expectedLeft, final.presentation.sourceBounds.left);
                            Assert::AreEqual(expectedTop, final.presentation.sourceBounds.top);
                            Assert::AreEqual(expectedRight, final.presentation.sourceBounds.right);
                            Assert::AreEqual(expectedEnvelopeBottom,
                                final.presentation.sourceBounds.bottom);
                            Assert::IsTrue(final.presentation.sourceBounds.bottom >=
                                result.captionBounds.bottom,
                                L"Final presentation must include every measured caption pixel.");
                            Assert::AreEqual(expectedBottom, final.state.trustedCrop.bottom,
                                L"Caption pixels must not replace physical picture authority.");
                            admittedExpectedEnvelope = true;
                        }
                        admission = accepted.state;
                        admittedExpectedCrop = true;
                    }
                }
            }
            if (assertExpectedCrop)
            {
                Assert::IsTrue(publishedExpectedCrop,
                    L"Native capture never published a stable trusted expected crop.");
                Assert::IsTrue(admittedExpectedCrop,
                    L"Native capture never admitted the expected crop for presentation.");
                if (assertExpectedEnvelope)
                    Assert::IsTrue(admittedExpectedEnvelope,
                        L"Native capture never admitted the guarded picture with its complete caption envelope.");
            }
        }
    };
}
