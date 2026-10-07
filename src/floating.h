// SPDX-License-Identifier: GPL-3.0-only

#ifndef __FLOATING_H_INCLUDED__
#define __FLOATING_H_INCLUDED__

#include "state.h"

#include <cairo.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

/**
 * Set up the floating selector: build the label symbols from the
 * configuration, fetch the selectable areas via the YOLO GUI detector, and
 * create the label selection and font face.
 */
void floating_enter(struct state *state, struct rect area);

/**
 * Handle a key press in the floating selector. Returns true if a redraw is
 * needed. Sets `state->running` to false when the selection is finished.
 */
bool floating_key(struct state *state, xkb_keysym_t keysym, char *text);

/**
 * Render the floating selector and the drag start marker.
 */
void floating_render(struct state *state, cairo_t *cairo);

/**
 * Free the floating selector's resources (areas, font face, label selection
 * and symbols).
 */
void floating_free(struct state *state);

#endif
