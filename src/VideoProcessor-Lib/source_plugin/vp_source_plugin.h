/*
 * VideoProcessor source plugin interface.
 *
 * A source plugin is a DLL in plugins/sources/ beside VideoProcessor.exe.
 * VP shows it as one more capture input. Without a plugin, VP behaves
 * exactly as before.
 *
 * Plain C with fixed-width fields so a plugin built with any compiler
 * (MinGW, MSVC, later GCC for a Linux .so) agrees with VP on the layout.
 * No C++ types, exceptions, CRT objects or allocations cross this boundary:
 * memory is freed by whoever allocated it.
 *
 * Entry point: the plugin exports one function, vp_source_plugin_get_api,
 * which returns its function table. VP refuses the plugin when the table's
 * abi_version differs from VP_SOURCE_PLUGIN_ABI_VERSION or its struct_size
 * is smaller than the table VP was built with.
 *
 * Threads (VP keeps all of its own threading; the plugin never calls VP
 * except through vp_source_host):
 *   - create, start, stop, get_state, send_command: VP's GUI thread.
 *   - destroy: any thread, once, after every frame is released (the last
 *     release can come from a renderer thread after stop).
 *   - wait_frame: VP's one reader thread for this instance, between start
 *     and stop only.
 *   - release_frame, clock_now, frames_*: any thread, at any time between
 *     create and destroy. VP may hold several frames at once and release
 *     them in any order, after stop has returned.
 *   - host->log: the plugin may call it from any thread.
 *
 * Timing: the plugin is the input's clock, as a capture card is. Every
 * frame timestamp is on that clock, in clock_ticks_per_second ticks.
 *
 * Frames: one block of tightly packed planes, no row padding.
 *   P010: Y plane (width * 2 bytes x height rows), then interleaved CbCr
 *         (width * 2 bytes x height / 2 rows). 10 bits in the high bits of
 *         each little-endian 16-bit word; low 6 bits zero.
 *   P010 is limited range. A plugin must not declare a frame it cannot
 *   deliver in this layout. An 8-bit source may be carried as P010 by an
 *   exact shift (value << 8, low 8 bits zero), declared with bit_depth 8;
 *   nothing else is converted (no chroma resampling, no range change).
 *
 * Reserved fields must be zero.
 */

#ifndef VP_SOURCE_PLUGIN_H
#define VP_SOURCE_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VP_SOURCE_PLUGIN_ABI_VERSION    1u

#if defined(_WIN32)
#define VP_SOURCE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define VP_SOURCE_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* Results. Every function that can fail returns one; negative is failure. */
#define VP_SOURCE_OK                0
#define VP_SOURCE_ERROR            -1   /* details went to host->log */
#define VP_SOURCE_ERROR_ARGUMENT   -2
#define VP_SOURCE_ERROR_STATE      -3   /* e.g. wait_frame before start */
#define VP_SOURCE_ERROR_UNSUPPORTED -4

/* wait_frame outcomes (non-negative). */
#define VP_SOURCE_WAIT_FRAME          0   /* *frame filled; release it later */
#define VP_SOURCE_WAIT_STATE_CHANGED  1   /* call get_state before the next
                                             wait; no frame was returned */
#define VP_SOURCE_WAIT_TIMEOUT        2
#define VP_SOURCE_WAIT_STOPPED        3   /* stop was called; return now */

/* Frame encodings a plugin may declare. Only plugins use these: VP's
 * capture cards never deliver them. */
#define VP_SOURCE_ENCODING_NONE   0u
#define VP_SOURCE_ENCODING_P010   1u

/* Transfer functions and colour spaces. Our own numbers, not VP's enum
 * values: the adapter maps each one by name, so VP can reorder its enums
 * without breaking a plugin. */
#define VP_SOURCE_EOTF_UNKNOWN    0u
#define VP_SOURCE_EOTF_SDR        1u
#define VP_SOURCE_EOTF_PQ         2u
#define VP_SOURCE_EOTF_HLG        3u

#define VP_SOURCE_COLORSPACE_UNKNOWN     0u
#define VP_SOURCE_COLORSPACE_REC_601_525 1u
#define VP_SOURCE_COLORSPACE_REC_601_625 2u
#define VP_SOURCE_COLORSPACE_REC_709     3u
#define VP_SOURCE_COLORSPACE_BT_2020     4u

/* vp_source_frame.flags */
#define VP_SOURCE_FRAME_DISCONTINUITY   (1u << 0)   /* first frame after a
                                                       seek, file change or
                                                       format change */

/* Log levels for host->log. */
#define VP_SOURCE_LOG_ERROR   0u
#define VP_SOURCE_LOG_WARNING 1u
#define VP_SOURCE_LOG_INFO    2u
#define VP_SOURCE_LOG_DEBUG   3u

/* HDR10 static metadata, in VP's units: CIE 1931 xy chromaticities and
 * cd/m2. has_mastering covers primaries, white point and both mastering
 * luminances; has_light_level covers MaxCLL and MaxFALL. */
typedef struct vp_source_hdr10 {
    uint32_t has_mastering;
    uint32_t has_light_level;
    double   red_x, red_y;
    double   green_x, green_y;
    double   blue_x, blue_y;
    double   white_x, white_y;
    double   max_mastering_luminance;
    double   min_mastering_luminance;
    double   max_cll;
    double   max_fall;
} vp_source_hdr10;

/* The decoder's own colour tags, untouched (libavutil AVCOL_* numbers).
 * VP does not render from these; it logs them, and they let VP check what
 * the plugin mapped into eotf and colorspace. set == 0: none known. */
typedef struct vp_source_raw_tags {
    uint32_t set;
    int32_t  primaries;
    int32_t  transfer;
    int32_t  matrix;
    int32_t  range;
    int32_t  chroma_location;
    uint32_t sar_num;           /* sample aspect ratio; 0/0 if unknown */
    uint32_t sar_den;
} vp_source_raw_tags;

/* The signal the input is delivering now: VP's VideoState and DisplayMode
 * in plain fields. valid == 0 means no signal; ignore the rest. */
typedef struct vp_source_state {
    uint32_t struct_size;       /* sizeof(vp_source_state), set by VP */
    uint32_t valid;
    uint32_t width;             /* pixels */
    uint32_t height;
    uint32_t time_scale;        /* ticks per second (DisplayMode) */
    uint32_t frame_duration;    /* ticks per frame, e.g. 1001 at 24000 */
    uint32_t encoding;          /* VP_SOURCE_ENCODING_* */
    uint32_t eotf;              /* VP_SOURCE_EOTF_* */
    uint32_t colorspace;        /* VP_SOURCE_COLORSPACE_* */
    uint32_t bit_depth;         /* the source's bits per sample: 8 or 10 (P010
                                   only); 0 = the encoding's own depth */
    vp_source_hdr10    hdr10;
    vp_source_raw_tags raw;
} vp_source_state;

/* One frame lent to VP. VP reads data until it calls release_frame with
 * the same handle; the plugin keeps the memory valid until then. */
typedef struct vp_source_frame {
    const void* data;           /* planes as described at the top */
    uint64_t    size;           /* bytes; must equal the state's frame size */
    uint64_t    counter;        /* monotonic; a gap means frames were missed */
    int64_t     timestamp;      /* plugin clock ticks: when this frame is
                                   due on screen */
    uint32_t    flags;          /* VP_SOURCE_FRAME_* */
    uint32_t    reserved0;
    void*       handle;         /* opaque, for release_frame */
} vp_source_frame;

/* Services VP offers the plugin. Valid from create until destroy returns. */
typedef struct vp_source_host {
    uint32_t abi_version;       /* VP_SOURCE_PLUGIN_ABI_VERSION */
    uint32_t struct_size;
    void*    context;           /* pass back unchanged */
    const char* plugin_dir;     /* UTF-8; the folder holding the plugin DLL */
    void (*log)(void* context, uint32_t level, const char* message_utf8);
} vp_source_host;

typedef struct vp_source_instance vp_source_instance;

/* The plugin's function table. Static storage in the plugin. */
typedef struct vp_source_plugin_api {
    uint32_t abi_version;       /* VP_SOURCE_PLUGIN_ABI_VERSION */
    uint32_t struct_size;       /* sizeof(vp_source_plugin_api) */
    const char* name;           /* UTF-8; the input's name in VP */

    int32_t (*create)(const vp_source_host* host, vp_source_instance** out);
    void    (*destroy)(vp_source_instance* inst);   /* any thread, after
                                                       every frame is
                                                       released */

    int32_t (*start)(vp_source_instance* inst);
    int32_t (*stop)(vp_source_instance* inst);      /* wakes wait_frame */

    int32_t (*get_state)(vp_source_instance* inst, vp_source_state* out);

    /* Waits up to timeout_ms. Returns VP_SOURCE_WAIT_* or a negative
     * result. The first call after start returns STATE_CHANGED. */
    int32_t (*wait_frame)(vp_source_instance* inst, uint32_t timeout_ms,
                          vp_source_frame* out);
    void    (*release_frame)(vp_source_instance* inst, void* handle);

    int64_t (*clock_now)(vp_source_instance* inst);
    int64_t (*clock_ticks_per_second)(vp_source_instance* inst);

    uint64_t (*frames_captured)(vp_source_instance* inst);
    uint64_t (*frames_missed)(vp_source_instance* inst);

    /* Player control: one UTF-8 command line, e.g. "pause", "seek +10".
     * VP_SOURCE_ERROR_UNSUPPORTED if the plugin does not know it. */
    int32_t (*send_command)(vp_source_instance* inst,
                            const char* command_utf8);
} vp_source_plugin_api;

/* The one exported symbol. Returns NULL if the plugin cannot run with
 * this host (it may log why). */
typedef const vp_source_plugin_api* (*vp_source_plugin_get_api_fn)(
    uint32_t host_abi_version);

#define VP_SOURCE_PLUGIN_ENTRY_NAME "vp_source_plugin_get_api"

#ifdef __cplusplus
}
#endif

#endif
