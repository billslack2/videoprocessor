#include "pch.h"
#include "CppUnitTest.h"

#include <ActivePictureEvidence.h>
#include <AnalysisDiagnosticTrace.h>
#include <SubtitleMeasurementWorker.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
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
            ActivePictureBounds rawBounds;
            uint64_t sequence = 0;
            int lines = 0;
            bool rawDark = false, arDark = false, subtitleProof = false;
            bool reset = false, repeated = false;
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
                a.policy == b.policy && a.continuity == b.continuity &&
                a.script == b.script && a.nearBarDistance == b.nearBarDistance &&
                a.optimizationMode == b.optimizationMode &&
                a.frameDuration100ns == b.frameDuration100ns;
        }

        class FreshReplay
        {
        public:
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
                result.subtitleProof = measured.subtitleArProof.IsCurrentFor(frame.source,
                    frame.identity, frame.policy, frame.continuity, frame.script);
                const auto evidence = result.subtitleProof ? measured.subtitleArProof.evidence : raw;
                const auto darkness = EvaluateActivePictureArDarkness(frame.source, evidence);
                result.rawDark = darkness.global.nearBlack;
                result.arDark = darkness.EffectiveNearBlack();
                const auto admitted = ConstrainNearBlackCropAcquisition(evidence, result.arDark);
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
            SubtitleBarTrackingReference m_prior;
            ReplayFrame m_previous;
            ReplayResult m_last;
            bool m_hasPrevious = false;
        };

        ReplayFrame ReadFrame(const std::filesystem::path& path)
        {
            ReplayFrame result;
            auto sidecar = path;
            sidecar.replace_extension(L".txt");
            std::ifstream metadata(sidecar);
            int format = 0, encoding = 0, colorspace = 0, top = 0, bottom = 0, script = 0;
            metadata >> result.source.width >> result.source.height >> result.source.rowBytes >>
                result.source.chromaRowBytes >> format >> encoding >> colorspace >> top >> bottom >> script;
            Assert::IsTrue(bool(metadata), L"The native frame sidecar is missing or malformed.");
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
                " repeat=" << result.repeated << " raw_class=" << int(result.rawClass) <<
                " native=" << result.rawBounds.left << ',' << result.rawBounds.top << '-' <<
                result.rawBounds.right << ',' << result.rawBounds.bottom <<
                " raw_dark=" << result.rawDark << " ar_dark=" << result.arDark <<
                " lines=" << result.lines << " subtitle_ar=" << result.subtitleProof <<
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
                expected = expectedReplay.Observe(frame);
                ReplayPacketOwner owner;
                owner.owner = frame.pixels;
                AnalysisDiagnosticFramePacketV1 packet;
                packet.width = frame.source.width; packet.height = frame.source.height;
                packet.stride0 = frame.source.rowBytes; packet.stride1 = frame.source.chromaRowBytes;
                packet.format = uint32_t(frame.source.format);
                packet.encoding = uint32_t(frame.source.encoding);
                packet.colorspace = uint32_t(frame.source.colorspace);
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
                const auto original = SyntheticFrame(100 + index, true);
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
            FreshReplay replay;
            for (const auto& path : frames)
            {
                const auto frame = ReadFrame(path);
                const auto result = replay.Observe(frame);
                Logger::WriteMessage(Describe(result).c_str());
            }
        }
    };
}
