/*
 * Copyright(C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "pch.h"
#include "CppUnitTest.h"
#include <VideoFrame.h>
#include <VideoState.h>
#include <CpuFeatures.h>
#include <video_frame_formatter/CR210DolbyVisionTunnelFormatter.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

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