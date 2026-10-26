/* Copyright(C) 2026 Bill Slack; SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <cstddef>
#include <cstdint>

// L5: ETSI GS CCM 001. Other layouts below are CANDIDATES from the
// independent CB1 HDMI serializer at d9ad989e, not Dolby conformance claims.
// Never use compressed-bitstream RPU lengths to interpret this byte-wide wire.
namespace DolbyVisionExtensionDiagnostics
{
    inline unsigned Word(const uint8_t* p) { return (unsigned(p[0]) << 8) | p[1]; }
    inline bool WordsFit(const uint8_t* p, size_t count, unsigned maximum = 4095)
    {
        for (size_t i = 0; i < count; ++i) if (Word(p + i * 2) > maximum) return false;
        return true;
    }
    inline const char* Layout(unsigned level, size_t bytes)
    {
        switch (level)
        {
        case 1: return bytes == 6 ? "etsi_L1" : "unsupported_length";
        case 2: return bytes == 14 ? "etsi_L2" : "unsupported_length";
        case 5: return bytes == 8 ? "etsi_L5" : "unsupported_length";
        case 3: return bytes == 6 ? "cb1_candidate" : "unsupported_length";
        case 4: return bytes == 4 ? "cb1_candidate" : "unsupported_length";
        case 8: return bytes == 13 || bytes == 15 || bytes == 17 || bytes == 23 || bytes == 29 ? "cb1_candidate" : "unsupported_length";
        case 9: return bytes == 1 || bytes == 17 ? "cb1_candidate" : "unsupported_length";
        case 10: return bytes == 6 || bytes == 22 ? "cb1_candidate" : "unsupported_length";
        case 11: return bytes == 4 ? "cb1_partial_candidate" : "unsupported_length";
        case 254: return bytes == 2 ? "cb1_candidate" : "unsupported_length";
        case 6: return "raw_only_no_qualified_HDMI_layout";
        default: return "raw_only_unknown_level";
        }
    }
    template<class Log>
    void Primaries(const uint8_t* p, unsigned long long frame, unsigned level, Log log);
    template<class Log>
    void Fields(unsigned level, const uint8_t* p, size_t bytes, uint64_t counter, Log log)
    {
        const auto frame = static_cast<unsigned long long>(counter);
        if (level == 3 && bytes == 6)
            log("DV metadata fields: frame=%llu L3 candidate=cb1 valid_codes=%u min_pq_offset_raw=%u max_pq_offset_raw=%u avg_pq_offset_raw=%u",
                frame, unsigned(WordsFit(p, 3)), Word(p), Word(p + 2), Word(p + 4));
        else if (level == 4 && bytes == 4)
            log("DV metadata fields: frame=%llu L4 candidate=cb1 valid_codes=%u anchor_pq=%u anchor_power=%u",
                frame, unsigned(WordsFit(p, 2)), Word(p), Word(p + 2));
        else if (level == 5 && bytes == 8)
            log("DV metadata fields: frame=%llu L5 valid_codes=%u left_offset=%u right_offset=%u top_offset=%u bottom_offset=%u geometry_checked=no",
                frame, unsigned(WordsFit(p, 4, 8191)), Word(p), Word(p + 2), Word(p + 4), Word(p + 6));
        else if (level == 8 && (bytes == 13 || bytes == 15 || bytes == 17 || bytes == 23 || bytes == 29))
        {
            const size_t words = bytes == 13 ? 6 : bytes == 15 ? 7 : 8;
            log("DV metadata fields: frame=%llu L8 candidate=cb1 valid_codes=%u target_display_index=%u target_nits=unresolved trim_slope=%u trim_offset=%u trim_power=%u trim_chroma_weight=%u trim_saturation_gain=%u ms_weight=%u mid_contrast_present=%u mid_contrast=%d clip_present=%u clip_trim=%d saturation_vectors_present=%u hue_vectors_present=%u",
                frame, unsigned(WordsFit(p + 1, words)), unsigned(p[0]), Word(p + 1), Word(p + 3), Word(p + 5), Word(p + 7), Word(p + 9), Word(p + 11),
                unsigned(bytes >= 15), bytes >= 15 ? int(Word(p + 13)) : -1,
                unsigned(bytes >= 17), bytes >= 17 ? int(Word(p + 15)) : -1, unsigned(bytes >= 23), unsigned(bytes == 29));
            if (bytes >= 23)
                log("DV metadata fields: frame=%llu L8 saturation_vector_raw=%u,%u,%u,%u,%u,%u",
                    frame, unsigned(p[17]), unsigned(p[18]), unsigned(p[19]), unsigned(p[20]), unsigned(p[21]), unsigned(p[22]));
            if (bytes == 29)
                log("DV metadata fields: frame=%llu L8 hue_vector_raw=%u,%u,%u,%u,%u,%u",
                    frame, unsigned(p[23]), unsigned(p[24]), unsigned(p[25]), unsigned(p[26]), unsigned(p[27]), unsigned(p[28]));
        }
        else if (level == 9 && (bytes == 1 || bytes == 17))
        {
            log("DV metadata fields: frame=%llu L9 candidate=cb1 source_primary_index=%u custom_present=%u index_length_consistent=%u",
                frame, unsigned(p[0]), unsigned(bytes == 17), unsigned((p[0] == 255) == (bytes == 17)));
            if (bytes == 17) Primaries(p + 1, frame, level, log);
        }
        else if (level == 10 && (bytes == 6 || bytes == 22))
        {
            log("DV metadata fields: frame=%llu L10 candidate=cb1 valid_pq_codes=%u target_display_index=%u target_max_pq=%u target_min_pq=%u target_primary_index=%u custom_present=%u index_length_consistent=%u",
                frame, unsigned(WordsFit(p + 1, 2) && Word(p + 3) <= Word(p + 1)), unsigned(p[0]), Word(p + 1), Word(p + 3), unsigned(p[5]), unsigned(bytes == 22), unsigned((p[5] == 255) == (bytes == 22)));
            if (bytes == 22) Primaries(p + 6, frame, level, log);
        }
        else if (level == 11 && bytes == 4)
            log("DV metadata fields: frame=%llu L11 candidate=cb1_partial content_type_raw=%u whitepoint_reference_packed_raw=%u reserved2_raw=%u reserved3_raw=%u bitfield_interpretation=unresolved",
                frame, unsigned(p[0]), unsigned(p[1]), unsigned(p[2]), unsigned(p[3]));
        else if (level == 254 && bytes == 2)
            log("DV metadata fields: frame=%llu L254 candidate=cb1 dm_mode=%u dm_version_index=%u",
                frame, unsigned(p[0]), unsigned(p[1]));
    }
    template<class Log>
    void Primaries(const uint8_t* p, unsigned long long frame, unsigned level, Log log)
    {
        log("DV metadata fields: frame=%llu L%u primaries_raw_rx_ry_gx_gy_bx_by_wx_wy=%u,%u,%u,%u,%u,%u,%u,%u",
            frame, level, Word(p), Word(p + 2), Word(p + 4), Word(p + 6), Word(p + 8), Word(p + 10), Word(p + 12), Word(p + 14));
    }
}
