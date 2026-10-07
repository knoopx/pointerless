// SPDX-License-Identifier: GPL-3.0-only

#include "floating.h"

#include "config.h"
#include "guidetect.h"
#include "log.h"
#include "screencopy.h"
#include "state.h"
#include "utils_cairo.h"

#include <cairo.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

static void get_area_from_guidetect(struct state *state, struct rect area) {
    // This is so that we don't capture window borders.
    area.x += 1;
    area.y += 1;
    area.h -= 2;
    area.w -= 2;

    struct scrcpy_buffer *scrcpy_buffer = query_screenshot(state, area);
    if (scrcpy_buffer == NULL) {
        LOG_ERR("Failed to capture screenshot.");
        state->areas     = NULL;
        state->num_areas = 0;
        return;
    }

    int err = gui_detect(
        state, scrcpy_buffer->data, scrcpy_buffer->width,
        scrcpy_buffer->height, (int)scrcpy_buffer->format, &state->areas,
        &state->num_areas
    );

    if (err == 0 && state->num_areas > 0) {
        // gui_detect returns boxes in capture-buffer pixel coordinates, but
        // the layer surface is drawn in logical (output) coordinates. On a
        // fractionally scaled output the buffer is larger than the logical
        // region, so convert the boxes to logical space with the
        // buffer-to-region scale (a no-op at scale 1).
        float sx = (float)area.w / scrcpy_buffer->width;
        float sy = (float)area.h / scrcpy_buffer->height;
        for (int i = 0; i < state->num_areas; i++) {
            struct rect *r = &state->areas[i];
            r->x = (int32_t)lround(r->x * sx);
            r->y = (int32_t)lround(r->y * sy);
            r->w = (int32_t)lround(r->w * sx);
            r->h = (int32_t)lround(r->h * sy);
        }

        LOG_DEBUG(
            "gui_detect: scaled %d box(es) from %dx%d buffer to %dx%d "
            "region.",
            state->num_areas, scrcpy_buffer->width, scrcpy_buffer->height,
            area.w, area.h
        );
    }

    destroy_scrcpy_buffer(scrcpy_buffer);

    if (err != 0) {
        LOG_ERR("GUI icon detection failed.");
    }
}

void floating_enter(struct state *state, struct rect area) {
    state->label_symbols =
        label_symbols_from_str(state->config.general.label_symbols);

    if (state->label_symbols == NULL) {
        state->areas           = NULL;
        state->label_selection = NULL;
        state->running         = false;
        return;
    }

    get_area_from_guidetect(state, area);

    state->label_selection =
        label_selection_new(state->label_symbols, state->num_areas);

    state->label_font_face = cairo_toy_font_face_create(
        state->config.general.label_font_family, CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL
    );
}

bool floating_key(struct state *state, xkb_keysym_t keysym, char *text) {
    // Keys can reach the surface before detection has produced labels;
    // ignore them rather than dereferencing a NULL label_symbols.
    if (state->label_symbols == NULL) {
        return false;
    }

    switch (keysym) {
    case XKB_KEY_BackSpace:
        return label_selection_back(state->label_selection) != 0;

    case XKB_KEY_Escape:
        state->running = false;
        break;
    default:;
        int symbol_idx = label_symbols_find_idx(state->label_symbols, text);
        if (symbol_idx < 0) {
            return false;
        }

        label_selection_append(state->label_selection, symbol_idx);

        int idx = label_selection_to_idx(state->label_selection);
        if (idx >= 0) {
            struct rect a = state->areas[idx];

            if (state->drag && state->drag_phase == 0) {
                // First pick: store the drag start point (the area's
                // left-center) and restart the selection for the second pick.
                state->drag_start_x = a.x;
                state->drag_start_y = a.y + a.h / 2;
                state->drag_phase   = 1;
                label_selection_clear(state->label_selection);
            } else if (state->drag && state->drag_phase == 1) {
                // Second pick: the area's right-center is the drag end point.
                state->click = CLICK_DRAG;
                struct rect end_point = {
                    .x = a.x + a.w - 1,
                    .y = a.y + a.h / 2,
                    .w = 1,
                    .h = 1,
                };
                memcpy(&state->result, &end_point, sizeof(struct rect));
                state->running = false;
            } else {
                memcpy(&state->result, &a, sizeof(struct rect));
                state->running = false;
            }
        }
        return true;
    }

    return false;
}

static void render_drag_mark(struct state *state, cairo_t *cairo) {
    if (state->drag_phase == 0) {
        return;
    }
    double                 s     = state->config.mode_drag.start_marker_size;
    enum drag_marker_shape shape =
        state->config.mode_drag.start_marker_shape;
    double x = state->drag_start_x;
    double y = state->drag_start_y;

    cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
    cairo_set_source_u32(cairo, state->config.mode_drag.start_marker_color);

    if (shape == DRAG_MARKER_CIRCLE) {
        cairo_arc(cairo, x, y, s, 0, 2 * M_PI);
        cairo_fill(cairo);
    } else if (shape == DRAG_MARKER_RECTANGLE) {
        // Rectangle: plain vertical bar
        double bar_w = s / 4.0 < 1.5 ? 1.5 : s / 4.0;
        double bar_h = s * 2.0;
        cairo_set_line_width(cairo, bar_w);
        cairo_set_line_cap(cairo, CAIRO_LINE_CAP_SQUARE);
        cairo_move_to(cairo, x, y - bar_h / 2);
        cairo_line_to(cairo, x, y + bar_h / 2);
        cairo_stroke(cairo);
    } else {
        // Caret: a vertical bar with serifs, like a text insertion cursor.
        // Bar: width = s/4 (min 1.5), height = s*2, centred on (x, y).
        double bar_w   = s / 4.0 < 1.5 ? 1.5 : s / 4.0;
        double bar_h   = s * 2.0;
        double serif_w = s * 0.75;
        cairo_set_line_width(cairo, bar_w);
        cairo_set_line_cap(cairo, CAIRO_LINE_CAP_SQUARE);

        // Vertical stroke
        cairo_move_to(cairo, x, y - bar_h / 2);
        cairo_line_to(cairo, x, y + bar_h / 2);
        cairo_stroke(cairo);

        // Top serif
        cairo_move_to(cairo, x - serif_w / 2, y - bar_h / 2);
        cairo_line_to(cairo, x + serif_w / 2, y - bar_h / 2);
        cairo_stroke(cairo);

        // Bottom serif
        cairo_move_to(cairo, x - serif_w / 2, y + bar_h / 2);
        cairo_line_to(cairo, x + serif_w / 2, y + bar_h / 2);
        cairo_stroke(cairo);
    }
}

// Draw order: largest area first so the biggest boxes land in the
// background and the smallest in the foreground. A key carries the
// box index so labels stay tied to their box, not the draw position.
struct area_key {
    double area;
    int    idx;
};

static int cmp_area_key_desc(const void *pa, const void *pb) {
    const struct area_key *a = (const struct area_key *)pa;
    const struct area_key *b = (const struct area_key *)pb;

    if (a->area < b->area) {
        return 1;
    }
    if (a->area > b->area) {
        return -1;
    }
    return a->idx < b->idx ? -1 : (a->idx > b->idx ? 1 : 0);
}

void floating_render(struct state *state, cairo_t *cairo) {
    struct general_config *config = &state->config.general;

    label_selection_t *curr_label =
        label_selection_new(state->label_symbols, state->num_areas);

    struct area_key *keys = NULL;
    if (state->num_areas > 0) {
        keys = malloc(sizeof(*keys) * (size_t)state->num_areas);
    }
    if (keys != NULL) {
        for (int i = 0; i < state->num_areas; i++) {
            keys[i].area =
                (double)state->areas[i].w * (double)state->areas[i].h;
            keys[i].idx = i;
        }
        qsort(
            keys, (size_t)state->num_areas, sizeof(*keys), cmp_area_key_desc
        );
    }

    int  label_str_max_len = label_selection_str_max_len(curr_label) + 1;
    char label_selected_str[label_str_max_len];
    char label_unselected_str[label_str_max_len];

    cairo_set_font_face(cairo, state->label_font_face);

    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_u32(cairo, config->unselectable_bg_color);
    cairo_paint(cairo);

    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    for (int i = 0; i < state->num_areas; i++) {
        const int box_idx = (keys != NULL) ? keys[i].idx : i;
        label_selection_set_from_idx(curr_label, box_idx);

        const bool selectable =
            label_selection_is_included(curr_label, state->label_selection);

        if (selectable) {
            const struct rect a = state->areas[box_idx];
            cairo_set_source_rgba(cairo, 0, 0, 0, 0);
            cairo_rectangle(cairo, a.x, a.y, a.w, a.h);
            cairo_fill(cairo);
        }
    }

    for (int i = 0; i < state->num_areas; i++) {
        const int box_idx = (keys != NULL) ? keys[i].idx : i;
        label_selection_set_from_idx(curr_label, box_idx);

        const struct rect a = state->areas[box_idx];

        const bool selectable =
            label_selection_is_included(curr_label, state->label_selection);

        if (selectable) {
            cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
            cairo_set_source_u32(cairo, config->selectable_bg_color);
            cairo_rectangle(cairo, a.x, a.y, a.w, a.h);
            cairo_fill(cairo);

            cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_u32(cairo, config->selectable_border_color);
            cairo_rectangle(cairo, a.x + .5, a.y + .5, a.w - 1, a.h - 1);
            cairo_set_line_width(cairo, 1);
            cairo_stroke(cairo);

            cairo_set_font_size(
                cairo,
                compute_relative_font_size(&config->label_font_size, a.h)
            );
            cairo_text_extents_t te_all;
            label_selection_str(curr_label, label_selected_str);
            cairo_text_extents(cairo, label_selected_str, &te_all);

            label_selection_str_split(
                curr_label, label_selected_str, label_unselected_str,
                state->label_selection->next
            );

            cairo_text_extents_t te_selected, te_unselected;
            cairo_text_extents(cairo, label_selected_str, &te_selected);
            cairo_text_extents(cairo, label_unselected_str, &te_unselected);

            cairo_move_to(
                cairo,
                a.x +
                    (a.w - te_selected.x_advance - te_unselected.x_advance) / 2,
                a.y + (int)((a.h + te_all.height) / 2)
            );
            cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
            cairo_set_source_u32(cairo, config->label_select_color);
            cairo_show_text(cairo, label_selected_str);
            cairo_set_source_u32(cairo, config->label_color);
            cairo_show_text(cairo, label_unselected_str);
        }
    }

    free(keys);
    label_selection_free(curr_label);

    render_drag_mark(state, cairo);
}

void floating_free(struct state *state) {
    free(state->areas);
    cairo_font_face_destroy(state->label_font_face);
    label_selection_free(state->label_selection);
    label_symbols_free(state->label_symbols);
}
