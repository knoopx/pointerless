// SPDX-License-Identifier: GPL-3.0-only

#ifndef __GUIDETECT_H_INCLUDED__
#define __GUIDETECT_H_INCLUDED__

#include <stdint.h>

#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

struct state;

/**
 * Load the NCNN YOLO net once from the given `.param` and `.bin` files.
 *
 * Subsequent calls are no-ops if the net is already loaded. Returns 0 on
 * success, non-zero on failure.
 */
int gui_detect_load(const char *param_path, const char *bin_path);

/**
 * Run GUI icon detection on raw screenshot pixels.
 *
 * `pixels` is the raw screencopy buffer (tightly packed 32-bit XRGB8888 /
 * ARGB8888 or XBGR8888 / ABGR8888 for the common case). `pixelformat` is the
 * `wl_shm_format` of the buffer. On success `*out_areas` is set to a
 * malloc'd array of detected UI boxes in original screenshot coordinates and
 * `*out_count` to its length (the caller frees `*out_areas`). The net is
 * loaded lazily from the bundled model location (the datadir `pointerless`
 * directory, set at build time via `GUIDETECT_MODEL_DIR`; a CWD fallback
 * when the macro is undefined) if not already loaded (see
 * `gui_detect_load`). Returns 0 on success, non-zero on failure.
 */
int gui_detect(
    struct state *state, const void *pixels, int w, int h, int pixelformat,
    struct rect **out_areas, int *out_count
);

#ifdef __cplusplus
}
#endif

#endif
