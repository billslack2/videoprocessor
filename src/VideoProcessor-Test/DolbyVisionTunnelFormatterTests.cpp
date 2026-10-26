/*
 * Copyright(C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "pch.h"
#include "CppUnitTest.h"
#include <VideoFrame.h>
#include <VideoState.h>
#include <CpuFeatures.h>
#include <DebugLog.h>
#include <video_frame_formatter/CR210DolbyVisionTunnelFormatter.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    namespace
    {
        using Packet = std::array<uint8_t, 128>;
        using Formatter = CR210DolbyVisionTunnelFormatter;

        // A forward sender, independent of the decoder's bit-mask expressions.
        // ETSI GS CCM 001 V1.1.1 sections 6.2-6.4 define the byte syntax,
        // scrambling and raster order. RGB8 packing follows HDMI 1.4a 6.5.4
        // and the independently published CroqueMr/libreelec-x86-DV packer:
        // d9ad989e, CB1/src/dvbridge_render.c, lines 629-633.
        uint32_t SenderCrc(const uint8_t* bytes, size_t count)
        {
            uint32_t remainder = 0xFFFFFFFFU;
            for (size_t byte = 0; byte < count; ++byte)
            {
                for (int bit = 7; bit >= 0; --bit)
                {
                    const bool feedback = ((remainder >> 31) ^
                        ((bytes[byte] >> bit) & 1U)) != 0;
                    remainder <<= 1;
                    if (feedback)
                        remainder ^= 0x04C11DB7U;
                }
            }
            return remainder;
        }

        void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value)
        {
            bytes[at] = static_cast<uint8_t>(value >> 8);
            bytes[at + 1] = static_cast<uint8_t>(value);
        }

        void Put32(uint8_t* bytes, uint32_t value)
        {
            for (unsigned byte = 0; byte < 4; ++byte)
                bytes[byte] = static_cast<uint8_t>(value >> (24 - byte * 8));
        }

        void FinishPacket(Packet& packet)
        {
            Put32(packet.data() + 124, SenderCrc(packet.data(), 124));
        }

        std::vector<uint8_t> DmBody(uint16_t peak = 3072,
            bool fullRange = true, size_t unknownPayload = 0)
        {
            std::vector<uint8_t> dm(71, 0);
            dm[1] = 1; // scene refresh
            const int16_t nonlinear[] = {
                8192, -4096, 2048, 0, 8192, -2048, 512, 256, 8192
            };
            const uint16_t linear[] = {
                16384, 0, 0, 0, 8192, 0, 0, 0, 4096
            };
            for (size_t i = 0; i < 9; ++i)
            {
                Put16(dm, 2 + i * 2, static_cast<uint16_t>(nonlinear[i]));
                Put16(dm, 32 + i * 2, linear[i]);
            }
            Put32(dm.data() + 20, 0x01000000U);
            Put32(dm.data() + 24, 0x08000000U);
            Put32(dm.data() + 28, 0x08000000U);
            dm[50] = dm[51] = 0xFF;
            dm[60] = 12;
            dm[61] = 2;
            dm[62] = 1;
            dm[63] = fullRange ? 1 : 0;
            Put16(dm, 64, 62);
            Put16(dm, 66, 3696);
            dm[69] = 42;
            dm[70] = unknownPayload ? 2 : 1;
            if (unknownPayload)
            {
                const size_t at = dm.size();
                dm.resize(at + 5 + unknownPayload, 0xA5);
                Put32(dm.data() + at, static_cast<uint32_t>(unknownPayload));
                dm[at + 4] = 200; // bounded unknown extension, deliberately skipped
            }
            const size_t at = dm.size();
            dm.resize(at + 11, 0);
            Put32(dm.data() + at, 6);
            dm[at + 4] = 1;
            Put16(dm, at + 5, 62);
            Put16(dm, at + 7, peak);
            Put16(dm, at + 9, 1024);
            return dm;
        }

        std::vector<Packet> Packetize(const std::vector<uint8_t>& body,
            uint8_t current = 3, uint8_t affected = 3,
            bool noMetadata = false, bool eos = false)
        {
            std::vector<Packet> packets;
            size_t offset = 0;
            do
            {
                Packet packet{};
                const bool first = packets.empty();
                const size_t capacity = first ? 119 : 121;
                const size_t chunk = noMetadata ? 0 :
                    (std::min)(capacity, body.size() - offset);
                const bool last = noMetadata || offset + chunk == body.size();
                const unsigned kind = first ? (last ? 0 : 1) : (last ? 3 : 2);
                packet[0] = static_cast<uint8_t>((kind << 6) | (noMetadata ? 1 : 0));
                packet[1] = static_cast<uint8_t>((affected << 4) | current);
                packet[2] = eos ? 1 : 0;
                if (first && !noMetadata)
                {
                    packet[3] = static_cast<uint8_t>(body.size() >> 8);
                    packet[4] = static_cast<uint8_t>(body.size());
                }
                if (chunk)
                    std::copy_n(body.data() + offset, chunk,
                        packet.data() + (first ? 5 : 3));
                FinishPacket(packet);
                packets.push_back(packet);
                offset += chunk;
                if (last)
                    break;
            } while (true);
            return packets;
        }

        struct SenderPicture
        {
            VideoStateComPtr state;
            std::vector<uint16_t> intensity, chroma;
            std::vector<BYTE> input;
            bool high8;

            SenderPicture(bool high, uint32_t width = 130, uint32_t height = 128)
                : high8(high)
            {
                state = new VideoState();
                state->valid = true;
                state->displayMode = std::make_shared<DisplayMode>(
                    width, height, false, 24000, 1001);
                state->videoFrameEncoding = VideoFrameEncoding::R210;
                state->dolbyVisionTunnel = true;
                intensity.resize(static_cast<size_t>(width) * height);
                chroma.resize(intensity.size());
                input.resize(state->BytesPerFrame(), 0xAD);
                for (size_t i = 0; i < intensity.size(); ++i)
                {
                    intensity[i] = static_cast<uint16_t>(i & 4095U);
                    chroma[i] = static_cast<uint16_t>((i * 73U + 31U) & 4095U);
                    Pack(i);
                }
            }

            void Pack(size_t pixel)
            {
                const unsigned y = intensity[pixel], c = chroma[pixel];
                const unsigned rgb8[] = { c / 16, y / 16,
                    (c % 16) * 16 + y % 16 };
                const unsigned factor = high8 ? 4 : 1;
                const uint32_t r210 = (rgb8[0] * factor << 20) |
                    (rgb8[1] * factor << 10) | (rgb8[2] * factor);
                const size_t width = state->displayMode->FrameWidth();
                Put32(input.data() + (pixel / width) * state->BytesPerRow() +
                    (pixel % width) * 4, r210);
            }

            void Embed(const std::vector<Packet>& packets)
            {
                Assert::IsTrue(packets.size() * 3072 <= intensity.size());
                for (size_t p = 0; p < packets.size(); ++p)
                {
                    for (size_t copy = 0; copy < 3; ++copy)
                    {
                        for (size_t bit = 0; bit < 1024; ++bit)
                        {
                            const size_t pixel = p * 3072 + copy * 1024 + bit;
                            const unsigned metadataBit =
                                (packets[p][bit / 8] >> (7 - bit % 8)) & 1U;
                            unsigned parity = 0;
                            for (unsigned b = 0; b < 12; ++b)
                            {
                                parity ^= (intensity[pixel] >> b) & 1U;
                                if (b != 0)
                                    parity ^= (chroma[pixel] >> b) & 1U;
                            }
                            chroma[pixel] = static_cast<uint16_t>(
                                (chroma[pixel] / 2) * 2 + (metadataBit ^ parity));
                            Pack(pixel);
                        }
                    }
                }
            }

            void CorruptCopy(size_t packet, size_t copy)
            {
                const size_t pixel = packet * 3072 + copy * 1024 + 17;
                chroma[pixel] ^= 1;
                Pack(pixel);
            }

            void CorruptAllCopies(size_t packet = 0)
            {
                for (size_t copy = 0; copy < 3; ++copy)
                    CorruptCopy(packet, copy);
            }
        };

        DolbyVisionTunnelFrameMetadata Deliver(Formatter& formatter,
            SenderPicture& picture, uint64_t counter,
            std::vector<BYTE>* converted = nullptr)
        {
            std::vector<BYTE> output(formatter.GetOutFrameSize(), 0xCD);
            VideoFrame frame(picture.input.data(), counter, 0, nullptr);
            Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
            if (converted)
                *converted = std::move(output);
            return formatter.GetDolbyVisionTunnelMetadata();
        }

        void AssertExactSamples(const SenderPicture& picture,
            const std::vector<BYTE>& output)
        {
            const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
            for (size_t pixel = 0; pixel < picture.intensity.size(); ++pixel)
            {
                Assert::AreEqual(static_cast<unsigned>(picture.intensity[pixel] * 16),
                    static_cast<unsigned>(samples[pixel]), L"12-bit intensity changed");
                Assert::AreEqual(static_cast<unsigned>(picture.chroma[pixel] * 16),
                    static_cast<unsigned>(samples[picture.intensity.size() + pixel]),
                    L"12-bit chroma changed");
            }
        }

        void AssertPeak(const DolbyVisionTunnelFrameMetadata& dm, uint16_t peak)
        {
            Assert::IsTrue(dm.valid && dm.hasSceneMetadata);
            Assert::AreEqual(static_cast<unsigned>(peak), static_cast<unsigned>(dm.sceneMaxPq));
            Assert::IsFalse(dm.allowSdrFallback);
        }
    }

    namespace
    {
        std::vector<std::string>* trimDiagnosticLines = nullptr;
        void __cdecl CaptureTrimDiagnostic(const char* message)
        {
            if (trimDiagnosticLines && (std::strstr(message, "DV trim diagnostic:") || std::strstr(message, "DV metadata ")))
                trimDiagnosticLines->emplace_back(message);
        }
        struct ScopedTrimDiagnosticSink
        {
            explicit ScopedTrimDiagnosticSink(std::vector<std::string>& lines)
            {
                trimDiagnosticLines = &lines;
                DebugLog::SetExternalSink(CaptureTrimDiagnostic);
            }
            ~ScopedTrimDiagnosticSink()
            {
                DebugLog::SetExternalSink(nullptr);
                trimDiagnosticLines = nullptr;
            }
        };
    }
    TEST_CLASS(DolbyVisionTrimDiagnosticsTests)
    {
        struct Trace
        {
            DolbyVisionTrimDiagnostics diagnostics;
            std::vector<std::string> lines;
            void Observe(const std::vector<uint8_t>& body, uint64_t time = 0)
            {
                diagnostics.Observe(body.data(), body.size(), 123, time, 3, 4, false,
                    [this](const char* format, auto... args) {
                        char text[2048];
                        std::snprintf(text, sizeof(text), format, args...);
                        lines.emplace_back(text);
                    });
            }
            bool Has(const char* text) const
            {
                return std::any_of(lines.begin(), lines.end(), [text](const auto& line) {
                    return line.find(text) != std::string::npos;
                });
            }
        };
        static void AddL2(std::vector<uint8_t>& body, unsigned target, unsigned slope, unsigned ms)
        {
            const size_t at = body.size();
            body.resize(at + 19);
            Put32(body.data() + at, 14);
            body[at + 4] = 2;
            ++body[70];
            const unsigned fields[] = {target, slope, 2048, 2048, 2048, 2048, ms};
            for (size_t i = 0; i < 7; ++i) Put16(body, at + 5 + i * 2, uint16_t(fields[i]));
        }
        static void AddBlock(std::vector<uint8_t>& body, unsigned level, const std::vector<uint8_t>& payload)
        {
            const size_t at = body.size();
            body.resize(at + 5);
            Put32(body.data() + at, static_cast<uint32_t>(payload.size()));
            body[at + 4] = static_cast<uint8_t>(level);
            ++body[70];
            body.insert(body.end(), payload.begin(), payload.end());
        }
    public:
        TEST_METHOD(ExtraLevelsDecodeBigEndianAndKeepAmbiguousFieldsRaw)
        {
            auto body = DmBody();
            AddBlock(body, 3, {8,1,8,2,8,3});
            AddBlock(body, 4, {7,255,8,0});
            AddBlock(body, 5, {0,1,0,2,1,0,1,1});
            AddBlock(body, 11, {2,24,0,0});
            AddBlock(body, 254, {0,2});
            Trace trace; trace.Observe(body);
            Assert::IsTrue(trace.Has("min_pq_offset_raw=2049 max_pq_offset_raw=2050 avg_pq_offset_raw=2051"));
            Assert::IsTrue(trace.Has("anchor_pq=2047 anchor_power=2048"));
            Assert::IsTrue(trace.Has("left_offset=1 right_offset=2 top_offset=256 bottom_offset=257"));
            Assert::IsTrue(trace.Has("whitepoint_reference_packed_raw=24"));
            Assert::IsTrue(trace.Has("bitfield_interpretation=unresolved"));
            Assert::IsTrue(trace.Has("dm_mode=0 dm_version_index=2"));
        }
        TEST_METHOD(L8OptionalFieldsArePresentOnlyAtQualifiedLengths)
        {
            for (const size_t bytes : {13,15,17,23,29})
            {
                std::vector<uint8_t> p(bytes, 0); p[0] = 42;
                for (size_t i=0; i<6; ++i) Put16(p, 1+i*2, uint16_t(2048+i));
                if (bytes>=15) Put16(p,13,2055);
                if (bytes>=17) Put16(p,15,2056);
                for (size_t i=17; i<bytes; ++i) p[i]=uint8_t(i);
                auto body = DmBody(); AddBlock(body,8,p);
                Trace trace; trace.Observe(body);
                Assert::IsTrue(trace.Has("L8 candidate=cb1 valid_codes=1 target_display_index=42 target_nits=unresolved"));
                Assert::IsTrue(trace.Has(bytes>=15 ? "mid_contrast_present=1 mid_contrast=2055" : "mid_contrast_present=0 mid_contrast=-1"));
                Assert::IsTrue(trace.Has(bytes>=17 ? "clip_present=1 clip_trim=2056" : "clip_present=0 clip_trim=-1"));
                Assert::AreEqual(bytes>=23,trace.Has("saturation_vector_raw=17,18,19,20,21,22"));
                Assert::AreEqual(bytes==29,trace.Has("hue_vector_raw=23,24,25,26,27,28"));
            }
        }
        TEST_METHOD(PrimariesAndCustomTargetKeepIndicesAndCoordinateWords)
        {
            auto body=DmBody();
            std::vector<uint8_t> p9(17,0); p9[0]=255;
            for(size_t i=0;i<8;++i) Put16(p9,1+2*i,uint16_t(1000+i));
            AddBlock(body,9,p9);
            std::vector<uint8_t> p10(22,0); p10[0]=42; p10[5]=255;
            Put16(p10,1,3072); Put16(p10,3,12);
            for(size_t i=0;i<8;++i) Put16(p10,6+2*i,uint16_t(2000+i));
            AddBlock(body,10,p10);
            Trace trace; trace.Observe(body);
            Assert::IsTrue(trace.Has("source_primary_index=255 custom_present=1 index_length_consistent=1"));
            Assert::IsTrue(trace.Has("target_display_index=42 target_max_pq=3072 target_min_pq=12"));
            Assert::IsTrue(trace.Has("primaries_raw_rx_ry_gx_gy_bx_by_wx_wy=1000,1001,1002,1003,1004,1005,1006,1007"));
            Assert::IsTrue(trace.Has("primaries_raw_rx_ry_gx_gy_bx_by_wx_wy=2000,2001,2002,2003,2004,2005,2006,2007"));
            body=DmBody(); AddBlock(body,9,{255}); AddBlock(body,10,{42,16,0,0,0,255});
            Trace bad; bad.Observe(body);
            Assert::IsTrue(bad.Has("custom_present=0 index_length_consistent=0"));
            Assert::IsTrue(bad.Has("L10 candidate=cb1 valid_pq_codes=0"));
        }
        TEST_METHOD(UnsupportedLengthsAndOutOfRangeValuesRemainEvidence)
        {
            auto body=DmBody();
            for (unsigned level : {3,4,5,8,9,10,11,254}) AddBlock(body,level,{});
            AddBlock(body,6,{1,2,3}); AddBlock(body,199,{4});
            Trace trace; trace.Observe(body);
            Assert::IsTrue(trace.Has("layout=unsupported_length"));
            Assert::IsTrue(trace.Has("raw_only_no_qualified_HDMI_layout"));
            Assert::IsTrue(trace.Has("level=199 payload_bytes=1 layout=raw_only_unknown_level"));
            Assert::IsFalse(trace.Has("DV metadata fields:"));
            body=DmBody(); AddBlock(body,4,{16,0,0,0}); AddBlock(body,5,{32,0,0,0,0,0,0,0});
            Trace invalid; invalid.Observe(body);
            Assert::IsTrue(invalid.Has("L4 candidate=cb1 valid_codes=0"));
            Assert::IsTrue(invalid.Has("L5 valid_codes=0"));
        }
        TEST_METHOD(InventorySurvivesSnapshotBudgetAndCountsDuplicateBodies)
        {
            auto body=DmBody(); Trace trace;
            for (unsigned i=0;i<129;++i) {body.back()=uint8_t(i);trace.Observe(body,i*1000ULL);}
            trace.lines.clear();
            AddBlock(body,254,{0,2});
            trace.Observe(body,130000);
            Assert::IsTrue(trace.Has("level=254 payload_bytes=2 layout=cb1_candidate"));
            Assert::IsTrue(trace.Has("dm_version_index=2"));
            Assert::IsFalse(trace.Has("raw_offset="));
            for(unsigned i=130;i<600;++i) trace.Observe(body,131000);
            Assert::IsTrue(trace.Has("last_complete_dm=600 L1=600"));
            Assert::IsTrue(trace.Has("L254=471 snapshot_budget_exhausted=1"));
        }
        TEST_METHOD(DecodesMultipleTargetsAndUnspecifiedMultiscaleWithoutApplyingTrims)
        {
            auto body = DmBody();
            AddL2(body, 2081, 2048, 65535);
            AddL2(body, 3072, 2200, 4095);
            Trace trace;
            trace.Observe(body);
            Assert::IsTrue(trace.Has("frame=123 received current_id=3 affected_id=4 association=next"));
            Assert::IsTrue(trace.Has("L2 valid=1 target_max_pq=2081 target_nits_approx=99.978"));
            Assert::IsTrue(trace.Has("trim_slope=2200"));
            Assert::IsTrue(trace.Has("ms_weight_raw=65535 ms_unspecified=1 five_trims_centered=1"));
            Assert::IsTrue(trace.Has("ms_weight_raw=4095 ms_unspecified=0 five_trims_centered=0"));
            Assert::IsTrue(trace.Has("L2_blocks=2 L8_blocks=0"));
            Assert::IsTrue(trace.Has("trims_applied=no"));
        }
        TEST_METHOD(FormatterLogsOnlyCompleteValidatedBodiesAndPreservesSamples)
        {
            auto body = DmBody(3072, true, 180);
            AddL2(body, 2081, 2200, 65535);
            AddBlock(body, 254, {0,2});
            SenderPicture picture(true);
            picture.Embed(Packetize(body));
            picture.CorruptAllCopies(1);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            std::vector<std::string> lines;
            ScopedTrimDiagnosticSink sink(lines);
            Deliver(formatter, picture, 10);
            Assert::IsTrue(lines.empty());
            picture.Embed(Packetize(body));
            std::vector<BYTE> output;
            AssertPeak(Deliver(formatter, picture, 11, &output), 3072);
            AssertExactSamples(picture, output);
            Assert::IsTrue(std::any_of(lines.begin(), lines.end(), [](const auto& line) {
                return line.find("trim_slope=2200") != std::string::npos;
            }));
            lines.clear();
            picture.Embed(Packetize({}, 3, 3, true));
            AssertPeak(Deliver(formatter, picture, 12), 3072);
            Assert::IsTrue(lines.empty());
        }        TEST_METHOD(InvalidTrimValuesRemainRawEvidence)
        {
            auto body = DmBody();
            AddL2(body, 5000, 2048, 32768);
            Trace trace;
            trace.Observe(body);
            Assert::IsTrue(trace.Has("L2 valid=0 target_max_pq=5000 target_nits_approx=-1.000"));
            Assert::IsTrue(trace.Has("ms_weight_raw=32768 ms_unspecified=0"));
        }
        TEST_METHOD(UnknownAndUnsupportedBlocksHaveCompleteReconstructableRawDump)
        {
            auto body = DmBody(3072, true, 700);
            body[75] = 8; // unknown L8 payload is retained without guessed interpretation
            AddL2(body, 2081, 2048, 65535);
            Trace trace;
            trace.Observe(body);
            std::vector<uint8_t> reconstructed;
            for (const auto& line : trace.lines)
            {
                const auto hex = line.find(" hex=");
                if (hex == std::string::npos || line.find("raw_offset=") == std::string::npos) continue;
                for (size_t i = hex + 5; i < line.size(); i += 2)
                    reconstructed.push_back(uint8_t(std::stoul(line.substr(i, 2), nullptr, 16)));
            }
            Assert::IsTrue(body == reconstructed);
            Assert::IsTrue(trace.Has("L2_blocks=1 L8_blocks=1"));
            // Structurally valid unsupported L2 length stays raw-only.
            body[75] = 2;
            Trace other;
            other.Observe(body);
            Assert::IsTrue(other.Has("decode=unsupported_L2_length"));
        }
        TEST_METHOD(TruncatedBodiesEmitNoPartialTransaction)
        {
            auto body = DmBody();
            AddL2(body, 2081, 2048, 65535);
            Trace trace;
            body.pop_back();
            trace.Observe(body);
            Assert::IsTrue(trace.lines.empty());
            body.resize(70);
            trace.Observe(body);
            Assert::IsTrue(trace.lines.empty());
        }
        TEST_METHOD(DuplicateAndRateLimitsAllowDeferredChangedSnapshot)
        {
            auto body = DmBody();
            Trace trace;
            trace.Observe(body);
            trace.lines.clear();
            trace.Observe(body, 2000);
            Assert::IsTrue(trace.lines.empty());
            body.back() = 1;
            trace.Observe(body, 999);
            Assert::IsTrue(trace.lines.empty());
            trace.Observe(body, 1000);
            Assert::IsTrue(trace.Has("snapshot=2"));
            Assert::IsTrue(trace.Has("rate_skipped_packets=1"));
        }
        TEST_METHOD(SnapshotBudgetReportsOnceAndResetRearms)
        {
            auto body = DmBody();
            Trace trace;
            for (unsigned i = 0; i < DolbyVisionTrimDiagnostics::MaxSnapshots; ++i)
            {
                body.back() = uint8_t(i);
                trace.Observe(body, i * 1000ULL);
                trace.lines.clear();
            }
            body.back() = 240;
            trace.Observe(body, 200000);
            Assert::IsTrue(trace.Has("budget exhausted snapshots=128"));
            trace.lines.clear();
            trace.Observe(body, 201000);
            Assert::IsTrue(trace.lines.empty());
            trace.diagnostics.Reset();
            trace.Observe(body, 202000);
            Assert::IsTrue(trace.Has("snapshot=1"));
        }
        TEST_METHOD(ByteBudgetRejectsWholeSnapshotWithoutPartialDump)
        {
            auto body = DmBody(3072, true, DolbyVisionTrimDiagnostics::MaxBodyBytes - 87);
            Assert::AreEqual(DolbyVisionTrimDiagnostics::MaxBodyBytes, body.size());
            Trace trace;
            for (unsigned i = 0; i < 10; ++i)
            {
                body.back() = uint8_t(i);
                trace.Observe(body, i * 1000ULL);
                trace.lines.clear();
            }
            body.back() = 20;
            trace.Observe(body, 10000);
            Assert::IsTrue(trace.Has("budget exhausted snapshots=10"));
            Assert::IsFalse(trace.Has("hex="));
        }
    };
    TEST_CLASS(DolbyVisionTunnelFormatterTests)
    {
    public:
        TEST_METHOD(IndependentSenderHasKnownCrcAndPackingAnchors)
        {
            const uint8_t reference[] = { '1','2','3','4','5','6','7','8','9' };
            Assert::AreEqual(0x0376E6E7U, SenderCrc(reference, sizeof(reference)));
            const auto packets = Packetize(DmBody());
            // Fixed footer of the complete single-packet fixture, not a roundtrip assertion.
            const uint8_t footer[] = { 0xBD, 0x58, 0x9A, 0x44 };
            Assert::IsTrue(std::equal(footer, footer + 4, packets[0].data() + 124));
            Assert::AreEqual(0U, SenderCrc(packets[0].data(), packets[0].size()));
            SenderPicture picture(false);
            // Y=0xABC, C=0x123 => RGB8=(0x12,0xAB,0x3C), r210=0x0122AC3C.
            picture.intensity[3073] = 0xABC;
            picture.chroma[3073] = 0x123;
            picture.Pack(3073);
            const size_t at = (3073 / 130) * picture.state->BytesPerRow() + (3073 % 130) * 4;
            const uint8_t known[] = { 0x01, 0x22, 0xAC, 0x3C };
            Assert::IsTrue(std::equal(known, known + 4, picture.input.data() + at));
            picture.Embed(packets);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            std::vector<BYTE> output;
            AssertPeak(Deliver(formatter, picture, 1, &output), 3072);
            AssertExactSamples(picture, output);
        }

        TEST_METHOD(SinglePacketReadsSignedMatricesOffsetsAndRange)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8);
                picture.Embed(Packetize(DmBody(3072, false)));
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                const auto dm = Deliver(formatter, picture, 1);
                AssertPeak(dm, 3072);
                Assert::IsFalse(dm.fullRange);
                Assert::AreEqual(-0.5f, dm.nonlinear[0][1]);
                Assert::AreEqual(0.25f, dm.nonlinear[0][2]);
                Assert::AreEqual(0.0625f, dm.nonlinearOffset[0]);
                Assert::AreEqual(0.5f, dm.nonlinearOffset[1]);
                Assert::AreEqual(1.0f, dm.linear[0][0]);
                Assert::AreEqual(0.25f, dm.linear[2][2]);
                Assert::AreEqual(62U, static_cast<unsigned>(dm.sourceMinPq));
                Assert::AreEqual(3696U, static_cast<unsigned>(dm.sourceMaxPq));
                Assert::AreEqual(high8 ? 1U : 2U, static_cast<unsigned>(dm.rgbBitAlignment));
                const auto contract = formatter.GetOutputContract();
                Assert::IsTrue(contract.sampleRange == VideoFrameSampleRange::LIMITED);
                Assert::AreEqual(12U, static_cast<unsigned>(contract.colorDepth));
                Assert::AreEqual(4U, static_cast<unsigned>(contract.bitShift));
            }
        }

        TEST_METHOD(AllTwelveBitCodesSurviveScalarAndAvx2WithPaddingAndTails)
        {
            for (const bool high8 : { false, true })
            {
                // DisplayMode requires both dimensions >=100. Cover 0/2/6 SIMD tail pixels.
                for (const unsigned width : { 128U, 130U, 134U })
                {
                    SenderPicture picture(high8, width, 100);
                    picture.Embed(Packetize(DmBody()));
                    for (const auto method : { Formatter::ConversionMethod::SCALAR,
                        Formatter::ConversionMethod::AVX2 })
                    {
                        if (method == Formatter::ConversionMethod::AVX2 &&
                            !CpuFeatures::SupportsAvx2Kernels())
                        {
                            Logger::WriteMessage(L"AVX2 qualification skipped: CPU/OS lacks AVX2 support" );
                            continue;
                        }
                        Formatter formatter;
                        formatter.SetConversionMethod(method);
                        formatter.OnVideoState(picture.state);
                        std::vector<BYTE> output;
                        AssertPeak(Deliver(formatter, picture, 1, &output), 3072);
                        AssertExactSamples(picture, output);
                    }
                }
            }
        }

        TEST_METHOD(ThreadedRowsAndUnevenTailPreserveEverySample)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8, 1922, 1081);
                picture.Embed(Packetize(DmBody()));
                for (const auto method : { Formatter::ConversionMethod::SCALAR,
                    Formatter::ConversionMethod::AVX2 })
                {
                    if (method == Formatter::ConversionMethod::AVX2 &&
                        !CpuFeatures::SupportsAvx2Kernels())
                    {
                        Logger::WriteMessage(L"AVX2 qualification skipped: CPU/OS lacks AVX2 support" );
                        continue;
                    }
                    Formatter formatter;
                    formatter.SetConversionMethod(method);
                    formatter.OnVideoState(picture.state);
                    std::vector<BYTE> output;
                    AssertPeak(Deliver(formatter, picture, 1, &output), 3072);
                    AssertExactSamples(picture, output);
                }
            }
        }

        TEST_METHOD(AlignmentChangeWithNoMetadataUsesNewLayoutAndActiveDm)
        {
            SenderPicture high(true), low(false);
            Formatter formatter;
            formatter.OnVideoState(high.state);
            high.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, high, 1), 3072);
            low.Embed(Packetize({}, 3, 3, true));
            std::vector<BYTE> output;
            AssertPeak(Deliver(formatter, low, 2, &output), 3072);
            AssertExactSamples(low, output);
            high.Embed(Packetize({}, 3, 3, true));
            AssertPeak(Deliver(formatter, high, 3, &output), 3072);
            AssertExactSamples(high, output);
        }
        TEST_METHOD(ContinuationBoundariesAndUnknownBlocksPreserveL1)
        {
            // Body lengths 119, 120, 124, 244, 512. L1 crosses both first
            // and later packet boundaries; 512 is the ETSI five-packet example.
            for (const size_t unknown : { size_t(32), size_t(33), size_t(37),
                size_t(157), size_t(425) })
            {
                for (const bool high8 : { false, true })
                {
                    SenderPicture picture(high8);
                    picture.Embed(Packetize(DmBody(2048, true, unknown)));
                    Formatter formatter;
                    formatter.OnVideoState(picture.state);
                    const auto dm = Deliver(formatter, picture, 1);
                    AssertPeak(dm, 2048);
                    Assert::AreEqual(1024U, static_cast<unsigned>(dm.sceneAvgPq));
                }
            }
        }

        TEST_METHOD(RepeatedCopiesRecoverFirstAndContinuationCorruption)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8);
                picture.Embed(Packetize(DmBody(2048, true, 157)));
                picture.CorruptCopy(0, 0);
                picture.CorruptCopy(0, 1);
                picture.CorruptCopy(1, 0);
                picture.CorruptCopy(2, 0);
                picture.CorruptCopy(2, 1);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                AssertPeak(Deliver(formatter, picture, 1), 2048);
            }
        }

        TEST_METHOD(DamagedContinuationNeverReplacesActiveMetadata)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            picture.Embed(Packetize(DmBody(2048, true, 157)));
            picture.CorruptAllCopies(1);
            AssertPeak(Deliver(formatter, picture, 2), 3072);
            picture.Embed(Packetize(DmBody(2048, true, 157)));
            AssertPeak(Deliver(formatter, picture, 3), 2048);
        }

        TEST_METHOD(MismatchedContinuationIdsFlagsAndKindsAreRejected)
        {
            for (const size_t field : { size_t(0), size_t(1), size_t(2) })
            {
                SenderPicture picture(true);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                picture.Embed(Packetize(DmBody()));
                AssertPeak(Deliver(formatter, picture, 1), 3072);
                auto packets = Packetize(DmBody(2048, true, 157));
                packets[1][field] ^= field == 0 ? 0x40 : 1;
                FinishPacket(packets[1]);
                picture.Embed(packets);
                AssertPeak(Deliver(formatter, picture, 2), 3072);
            }
        }

        TEST_METHOD(DeclaredLengthBoundsAndMalformedExtensionsDoNotReplaceDm)
        {
            for (const uint16_t declared : { uint16_t(70), uint16_t(120), uint16_t(0x2F01) })
            {
                SenderPicture picture(false);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                picture.Embed(Packetize(DmBody()));
                AssertPeak(Deliver(formatter, picture, 1), 3072);
                auto packets = Packetize(DmBody(2048));
                packets[0][3] = static_cast<uint8_t>(declared >> 8);
                packets[0][4] = static_cast<uint8_t>(declared);
                FinishPacket(packets[0]);
                picture.Embed(packets);
                AssertPeak(Deliver(formatter, picture, 2), 3072);
            }
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            auto bad = DmBody(2048);
            Put32(bad.data() + 71, 7); // six payload bytes available
            picture.Embed(Packetize(bad));
            AssertPeak(Deliver(formatter, picture, 2), 3072);
            bad = DmBody(4096); // L1 values must be 12-bit
            picture.Embed(Packetize(bad));
            AssertPeak(Deliver(formatter, picture, 3), 3072);
        }

        TEST_METHOD(DeclaredPacketCountCannotReadBeyondActiveRaster)
        {
            SenderPicture picture(false, 100, 100); // minimum mode; only three packets fit
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            auto packets = Packetize(DmBody());
            packets[0][0] = 0x40; // valid first-of-many kind
            packets[0][3] = 2;
            packets[0][4] = 0; // valid declared 512-byte body needs five packets
            FinishPacket(packets[0]);
            picture.Embed(packets);
            AssertPeak(Deliver(formatter, picture, 2), 3072);
        }

        TEST_METHOD(UnsupportedHeaderCannotAuthorizeDolbyVisionLock)
        {
            // Reserved metadata type/version, reserved EOS bits, and an
            // affected ID that is neither current nor immediate next.
            for (const unsigned variant : { 0U, 1U, 2U, 3U })
            {
                SenderPicture picture(false);
                auto packets = Packetize(DmBody());
                if (variant == 0) packets[0][0] |= 0x10;
                if (variant == 1) packets[0][0] |= 0x02;
                if (variant == 2) packets[0][2] |= 0x02;
                if (variant == 3) packets[0][1] = 0x53;
                FinishPacket(packets[0]);
                picture.Embed(packets);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                const auto dm = Deliver(formatter, picture, 1);
                Assert::IsFalse(dm.valid);
                Assert::IsTrue(dm.allowSdrFallback);
            }
        }
        TEST_METHOD(ValidHeaderBlocksSdrEvenWhenBodyIsUnsupported)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            auto body = DmBody();
            body[60] = 10; // unsupported tunnel code depth
            picture.Embed(Packetize(body));
            const auto dm = Deliver(formatter, picture, 1);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
        }

        TEST_METHOD(NoMetadataRetainsActiveDmAndDoesNotResurrectAbsentDm)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize({}, 3, 3, true));
            auto dm = Deliver(formatter, picture, 1);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 2), 3072);
            for (uint64_t counter = 3; counter < 40; ++counter)
            {
                picture.Embed(Packetize({}, 3, 3, true));
                AssertPeak(Deliver(formatter, picture, counter), 3072);
            }
            picture.Embed(Packetize({}, 4, 4, true));
            dm = Deliver(formatter, picture, 40);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
        }

        TEST_METHOD(AdvanceMetadataAppliesOnlyToImmediateNextPicture)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody(), 15, 15));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize(DmBody(2048), 15, 0)); // ID wrap
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            picture.Embed(Packetize({}, 0, 0, true));
            picture.CorruptAllCopies(); // contiguous counter alone associates pending DM
            AssertPeak(Deliver(formatter, picture, 12), 2048);
        }

        TEST_METHOD(DroppedNextPictureWaitsForHeaderNamingAffectedId)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody(), 1, 1));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize(DmBody(2048), 1, 2));
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            picture.CorruptAllCopies();
            AssertPeak(Deliver(formatter, picture, 13), 3072);
            picture.Embed(Packetize({}, 2, 2, true));
            AssertPeak(Deliver(formatter, picture, 14), 2048);
        }

        TEST_METHOD(UnrelatedCurrentIdDiscardsPendingAndActiveDm)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody(), 1, 1));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize(DmBody(2048), 1, 2));
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            picture.Embed(Packetize({}, 3, 3, true));
            const auto dm = Deliver(formatter, picture, 13);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            picture.Embed(Packetize({}, 2, 2, true));
            Assert::IsFalse(Deliver(formatter, picture, 14).valid);
        }

        TEST_METHOD(EosNoMetadataAllowsSdrAfterBoundaryIncludingCounterGap)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize({}, 3, 3, true, true));
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            // A blank RGB frame contains no CRC-valid metadata.
            std::fill(picture.input.begin(), picture.input.end(), 0);
            const auto dm = Deliver(formatter, picture, 14);
            Assert::IsFalse(dm.valid);
            Assert::IsTrue(dm.allowSdrFallback);
            picture.Embed(Packetize(DmBody(2048)));
            AssertPeak(Deliver(formatter, picture, 15), 2048);
        }

        TEST_METHOD(MovieExitBlanksFirstUntrustedPictureButRetainsDmForRecovery)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                picture.Embed(Packetize(DmBody()));
                Assert::IsFalse(Deliver(formatter, picture, 1).requiresBlackFrame);

                // HDMI goes blank before capture publishes its new menu mode.
                // Cached DV decoding of this RGB picture used to create color.
                std::fill(picture.input.begin(), picture.input.end(), 0);
                const auto lost = Deliver(formatter, picture, 2);
                AssertPeak(lost, 3072); // cache survives a brief interruption
                Assert::IsTrue(lost.requiresBlackFrame);
                Assert::IsFalse(lost.allowSdrFallback);

                SenderPicture resumed(high8);
                resumed.Embed(Packetize({}, 3, 3, true));
                std::vector<BYTE> output;
                const auto recovered = Deliver(formatter, resumed, 3, &output);
                AssertPeak(recovered, 3072);
                Assert::IsFalse(recovered.requiresBlackFrame);
                AssertExactSamples(resumed, output);
            }
        }

        TEST_METHOD(BlankingPersistsThroughLostDmAndStopsOnFreshDmOrEos)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            Deliver(formatter, picture, 1);
            std::fill(picture.input.begin(), picture.input.end(), 0);
            for (uint64_t counter = 2; counter <= 25; ++counter)
                Assert::IsTrue(Deliver(formatter, picture, counter).requiresBlackFrame);
            picture.Embed(Packetize({}, 3, 3, true));
            const auto noDm = Deliver(formatter, picture, 26);
            Assert::IsFalse(noDm.valid);
            Assert::IsTrue(noDm.requiresBlackFrame);
            picture.Embed(Packetize(DmBody(2048)));
            Assert::IsFalse(Deliver(formatter, picture, 27).requiresBlackFrame);
            picture.Embed(Packetize({}, 3, 3, true, true));
            Assert::IsFalse(Deliver(formatter, picture, 28).requiresBlackFrame);
            std::fill(picture.input.begin(), picture.input.end(), 0);
            const auto sdr = Deliver(formatter, picture, 29);
            Assert::IsFalse(sdr.requiresBlackFrame);
            Assert::IsTrue(sdr.allowSdrFallback);
        }

        TEST_METHOD(RecoveredHeaderAndDamagedContinuationDoNotBlankKnownDvPixels)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            Deliver(formatter, picture, 1);
            picture.Embed(Packetize(DmBody(2048, true, 157)));
            picture.CorruptCopy(0, 0);
            picture.CorruptCopy(0, 1); // third header copy still authenticates DV
            picture.CorruptAllCopies(1); // old DM remains usable for same ID
            const auto dm = Deliver(formatter, picture, 2);
            AssertPeak(dm, 3072);
            Assert::IsFalse(dm.requiresBlackFrame);
        }

        TEST_METHOD(LossLimitSuspendsUntilFreshCompleteDmWithoutSdrFallback)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            picture.CorruptAllCopies();
            for (uint64_t counter = 2; counter < 25; ++counter)
                AssertPeak(Deliver(formatter, picture, counter), 3072);
            std::vector<BYTE> output;
            auto dm = Deliver(formatter, picture, 25, &output);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            Assert::IsTrue(std::all_of(output.begin(), output.end(), [](BYTE b) { return b == 0; }));
            picture.Embed(Packetize({}, 3, 3, true));
            dm = Deliver(formatter, picture, 26);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            picture.Embed(Packetize(DmBody(2048)));
            AssertPeak(Deliver(formatter, picture, 27), 2048);
            formatter.OnVideoState(picture.state);
            std::fill(picture.input.begin(), picture.input.end(), 0);
            dm = Deliver(formatter, picture, 28);
            Assert::IsFalse(dm.valid);
            Assert::IsTrue(dm.allowSdrFallback);
        }
    };
}