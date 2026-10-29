#pragma once
#include "CppUnitTest.h"
#include <VideoFrame.h>
#include <VideoState.h>
#include <video_frame_formatter/CRGBDolbyVisionTunnelFormatter.h>
#include <algorithm>
#include <array>
#include <vector>

// Test-only independent HDMI sender, shared by formatter and SDK-capture tests.
namespace { namespace DolbyVisionTestSender {
using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
using Packet = std::array<uint8_t, 128>;
using Formatter = CRGBDolbyVisionTunnelFormatter;
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

            SenderPicture(bool high, uint32_t width = 130, uint32_t height = 128,
                VideoFrameEncoding encoding = VideoFrameEncoding::R210)
                : high8(high)
            {
                state = new VideoState();
                state->valid = true;
                state->displayMode = std::make_shared<DisplayMode>(
                    width, height, false, 24000, 1001);
                state->videoFrameEncoding = encoding;
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
                auto* destination = input.data() + (pixel / width) * state->BytesPerRow() +
                    (pixel % width) * 4;
                // Independent forward sender: alpha is unrelated to tunnel parity.
                const uint8_t alpha = static_cast<uint8_t>(pixel * 37);
                if (state->videoFrameEncoding == VideoFrameEncoding::ARGB_8BIT)
                {
                    destination[0] = alpha; destination[1] = uint8_t(rgb8[0]);
                    destination[2] = uint8_t(rgb8[1]); destination[3] = uint8_t(rgb8[2]);
                }
                else if (state->videoFrameEncoding == VideoFrameEncoding::BGRA_8BIT)
                {
                    destination[0] = uint8_t(rgb8[2]); destination[1] = uint8_t(rgb8[1]);
                    destination[2] = uint8_t(rgb8[0]); destination[3] = alpha;
                }
                else
                    Put32(destination, r210);
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
                const unsigned expectedY = picture.intensity[pixel] * 16;
                const unsigned expectedC = picture.chroma[pixel] * 16;
                // Check every pixel, entering the test framework only on error.
                if (samples[pixel] != expectedY)
                    Assert::AreEqual(expectedY, unsigned(samples[pixel]), L"12-bit intensity changed");
                if (samples[picture.intensity.size() + pixel] != expectedC)
                    Assert::AreEqual(expectedC, unsigned(samples[picture.intensity.size() + pixel]),
                        L"12-bit chroma changed");
            }
        }

        void AssertPeak(const DolbyVisionTunnelFrameMetadata& dm, uint16_t peak)
        {
            Assert::IsTrue(dm.valid && dm.hasSceneMetadata);
            Assert::AreEqual(static_cast<unsigned>(peak), static_cast<unsigned>(dm.sceneMaxPq));
            Assert::IsFalse(dm.allowSdrFallback);
        }
} }
