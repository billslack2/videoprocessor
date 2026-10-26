/* Copyright(C) 2026 Bill Slack; SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "DolbyVisionExtensionDiagnostics.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// Diagnostic observer only: never changes metadata acceptance or rendering.
// L2 wire layout: ETSI GS CCM 001 V1.1.1, tables 3-5 and section 6.2.2.
class DolbyVisionTrimDiagnostics
{
public:
    static constexpr size_t MaxBodyBytes = 0x2F00;
    static constexpr unsigned MaxSnapshots = 128;
    static constexpr size_t MaxDumpBytes = 128 * 1024;
    static constexpr uint64_t MinIntervalMs = 1000;

    void Reset()
    {
        m_lastLength = 0;
        m_inventorySeen.fill(false);
        m_inventoryCounts.fill(0);
        m_inventoryBodies = 0;
        m_snapshots = 0;
        m_dumpBytes = 0;
        m_lastTime = 0;
        m_rateSkipped = 0;
        m_limitReported = false;
    }

    // Called only after CRC/reassembly/parser validation, never for no_md.
    // IDs describe receipt/current-vs-next association, not proof of display.
    template<class Log>
    void Observe(const uint8_t* body, size_t length, uint64_t frame,
        uint64_t nowMs, unsigned currentId, unsigned affectedId, bool eos, Log log)
    {
        if (!body || length < 71 || length > MaxBodyBytes)
            return;
        // Defensive bounds pass before emitting any part of a transaction.
        size_t at = 71;
        for (unsigned i = 0; i < body[70]; ++i)
        {
            if (length - at < 5) return;
            const size_t bytes = Read32(body + at);
            if (bytes > length - at - 5) return;
            at += 5 + bytes;
        }
        Inventory(body, frame, currentId, affectedId, log);
        if (m_limitReported) return;
        if (m_lastLength == length && std::memcmp(m_lastBody.data(), body, length) == 0)
            return;
        if (m_snapshots >= MaxSnapshots || length > MaxDumpBytes - m_dumpBytes)
        {
            if (!m_limitReported)
                log("DV trim diagnostic: budget exhausted snapshots=%u raw_bytes=%zu; restart capture to rearm",
                    m_snapshots, m_dumpBytes);
            m_limitReported = true;
            return;
        }
        if (m_snapshots && nowMs - m_lastTime < MinIntervalMs)
        {
            ++m_rateSkipped;
            return;
        }
        std::memcpy(m_lastBody.data(), body, length);
        m_lastLength = length;
        m_lastTime = nowMs;
        m_dumpBytes += length;
        const unsigned snapshot = ++m_snapshots;
        log("DV trim diagnostic: snapshot=%u frame=%llu received current_id=%u affected_id=%u association=%s eos=%u scene_refresh=%u bytes=%zu blocks=%u rate_skipped_packets=%llu trims_applied=no",
            snapshot, static_cast<unsigned long long>(frame), currentId, affectedId,
            currentId == affectedId ? "current" : "next", unsigned(eos), unsigned(body[1]),
            length, unsigned(body[70]), static_cast<unsigned long long>(m_rateSkipped));
        m_rateSkipped = 0;
        at = 71;
        unsigned l2Count = 0, l8Count = 0;
        for (unsigned i = 0; i < body[70]; ++i)
        {
            const size_t bytes = Read32(body + at);
            const unsigned level = body[at + 4];
            const uint8_t* p = body + at + 5;
            if (level == 2) ++l2Count;
            if (level == 8) ++l8Count;
            // Bound decoded lines even for a body with 255 small blocks.
            if (i < 16)
            {
                if (level == 1 && bytes == 6)
                    log("DV trim diagnostic: snapshot=%u block=%u L1 min_pq=%u max_pq=%u avg_pq=%u",
                        snapshot, i, Read16(p), Read16(p + 2), Read16(p + 4));
                else if (level == 2 && bytes == 14)
                {
                    const unsigned target = Read16(p), slope = Read16(p + 2), offset = Read16(p + 4);
                    const unsigned power = Read16(p + 6), chroma = Read16(p + 8), saturation = Read16(p + 10);
                    const unsigned ms = Read16(p + 12);
                    const bool valid = target <= 4095 && slope <= 4095 && offset <= 4095 &&
                        power <= 4095 && chroma <= 4095 && saturation <= 4095 && (ms <= 4095 || ms == 65535);
                    log("DV trim diagnostic: snapshot=%u block=%u L2 valid=%u target_max_pq=%u target_nits_approx=%.3f trim_slope=%u trim_offset=%u trim_power=%u trim_chroma_weight=%u trim_saturation_gain=%u ms_weight_raw=%u ms_unspecified=%u five_trims_centered=%u",
                        snapshot, i, unsigned(valid), target, target <= 4095 ? PqNits(target) : -1.0,
                        slope, offset, power, chroma, saturation, ms, unsigned(ms == 65535),
                        unsigned(slope == 2048 && offset == 2048 && power == 2048 && chroma == 2048 && saturation == 2048));
                }
                else
                    log("DV trim diagnostic: snapshot=%u block=%u level=%u payload_bytes=%zu decode=%s",
                        snapshot, i, level, bytes, level == 2 ? "unsupported_L2_length" : DolbyVisionExtensionDiagnostics::Layout(level, bytes));
            }
            if (i < 16) DolbyVisionExtensionDiagnostics::Fields(level, p, bytes, frame, log);
            at += 5 + bytes;
        }
        log("DV trim diagnostic: snapshot=%u L2_blocks=%u L8_blocks=%u decoded_block_limit=16 raw_complete_bytes=%zu",
            snapshot, l2Count, l8Count, length);
        // Every byte of the accepted DM body, including unknown extensions.
        // Numbered chunks allow missing async-log records to be detected offline.
        constexpr size_t chunkBytes = 256;
        constexpr char hex[] = "0123456789abcdef";
        char encoded[chunkBytes * 2 + 1];
        for (size_t start = 0; start < length; start += chunkBytes)
        {
            const size_t count = (std::min)(chunkBytes, length - start);
            for (size_t j = 0; j < count; ++j)
            {
                encoded[j * 2] = hex[body[start + j] >> 4];
                encoded[j * 2 + 1] = hex[body[start + j] & 15];
            }
            encoded[count * 2] = 0;
            log("DV trim diagnostic: snapshot=%u raw_offset=%zu raw_bytes=%zu hex=%s",
                snapshot, start, count, encoded);
        }
        log("DV trim diagnostic: snapshot=%u end", snapshot);
    }

private:
    // Independent of the changed-body dump budget: a later title can reveal
    // a new level after all regular snapshots have been consumed.
    template<class Log>
    void Inventory(const uint8_t* body, uint64_t frame,
        unsigned currentId, unsigned affectedId, Log log)
    {
        size_t at = 71;
        unsigned discoveries = 0;
        for (unsigned i = 0; i < body[70]; ++i)
        {
            const size_t bytes = Read32(body + at);
            const unsigned level = body[at + 4];
            const auto* p = body + at + 5;
            ++m_inventoryCounts[level];
            if (!m_inventorySeen[level] && discoveries < 16)
            {
                ++discoveries;
                m_inventorySeen[level] = true;
                char prefix[65];
                constexpr char hex[] = "0123456789abcdef";
                const size_t count = (std::min)(bytes, size_t(32));
                for (size_t j = 0; j < count; ++j)
                {
                    prefix[j * 2] = hex[p[j] >> 4];
                    prefix[j * 2 + 1] = hex[p[j] & 15];
                }
                prefix[count * 2] = 0;
                log("DV metadata inventory: frame=%llu current_id=%u affected_id=%u level=%u payload_bytes=%zu layout=%s first_payload_bytes=%zu payload_complete=%u hex=%s applied=no_by_diagnostics",
                    static_cast<unsigned long long>(frame), currentId, affectedId, level, bytes,
                    DolbyVisionExtensionDiagnostics::Layout(level, bytes), count, unsigned(count == bytes), prefix);
                DolbyVisionExtensionDiagnostics::Fields(level, p, bytes, frame, log);
            }
            at += 5 + bytes;
        }
        if (++m_inventoryBodies == 600)
        {
            log("DV metadata inventory: last_complete_dm=600 L1=%u L2=%u L3=%u L4=%u L5=%u L6=%u L8=%u L9=%u L10=%u L11=%u L254=%u snapshot_budget_exhausted=%u counts=block_occurrences",
                m_inventoryCounts[1],m_inventoryCounts[2],m_inventoryCounts[3],m_inventoryCounts[4],m_inventoryCounts[5],m_inventoryCounts[6],m_inventoryCounts[8],m_inventoryCounts[9],m_inventoryCounts[10],m_inventoryCounts[11],m_inventoryCounts[254],unsigned(m_limitReported));
            m_inventoryCounts.fill(0);
            m_inventoryBodies = 0;
        }
    }
    std::array<bool, 256> m_inventorySeen{};
    std::array<unsigned, 256> m_inventoryCounts{};
    unsigned m_inventoryBodies = 0;
    static unsigned Read16(const uint8_t* p) { return (unsigned(p[0]) << 8) | p[1]; }
    static uint32_t Read32(const uint8_t* p)
    {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    static double PqNits(unsigned pq)
    {
        // ETSI quantizes these codes using 4096; retain the raw code as authority.
        const double v = std::pow(pq / 4096.0, 32.0 / 2523.0);
        return 10000.0 * std::pow((std::max)(v - 3424.0 / 4096.0, 0.0) /
            (2413.0 / 128.0 - 2392.0 / 128.0 * v), 16384.0 / 2610.0);
    }
    std::array<uint8_t, MaxBodyBytes> m_lastBody{};
    size_t m_lastLength = 0, m_dumpBytes = 0;
    unsigned m_snapshots = 0;
    uint64_t m_lastTime = 0, m_rateSkipped = 0;
    bool m_limitReported = false;
};
