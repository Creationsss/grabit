// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "log.h"
#include "region/annotate.h"
#include "region/wlr_input_state.h"
#include "util/util.h"
#include "wl/wl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "region/render_internal.h"

void gren_output_redraw(struct ro_output *o) {
	if (!o->configured) return;
	o->dirty = false;

	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;

	struct grabit_shm_slot *slot = grabit_shm_pool_next(
		o->st->wls->shm, "grabit-region", &o->pool, pw, ph);
	if (!slot) {
		o->dirty = true;
		return;
	}
	cairo_surface_t *dst = grabit_cairo_image_argb(slot->buf.map, pw, ph, pw * 4);
	if (!dst) return;
	cairo_t *cr = cairo_create(dst);

	int32_t sel_l = 0, sel_t = 0, sel_r = 0, sel_b = 0;
	bool sel_visible = false;
	int32_t draw_x = 0, draw_y = 0, draw_w = 0, draw_h = 0;
	bool draw_any = false;
	bool draw_is_snap = false;
	if (o->st->has_selection) {
		draw_x = o->st->sel_x;
		draw_y = o->st->sel_y;
		draw_w = o->st->sel_w;
		draw_h = o->st->sel_h;
		draw_any = true;
	} else if (!o->st->region_locked && !o->st->dragging &&
			   o->st->snap_hover >= 0 &&
			   (size_t)o->st->snap_hover < o->st->n_snap_windows) {
		const struct rect *w = &o->st->snap_windows[o->st->snap_hover];
		draw_x = w->x;
		draw_y = w->y;
		draw_w = w->w;
		draw_h = w->h;
		draw_any = true;
		draw_is_snap = true;
	}
	if (draw_any) {
		int32_t sx = (draw_x - o->go->x) * S;
		int32_t sy = (draw_y - o->go->y) * S;
		int32_t sw = draw_w * S;
		int32_t sh = draw_h * S;
		sel_l = i32max(0, sx);
		sel_t = i32max(0, sy);
		sel_r = i32min(pw, sx + sw);
		sel_b = i32min(ph, sy + sh);
		sel_visible = (sel_r > sel_l && sel_b > sel_t);
	}

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	if (o->cairo_frozen_pat)
		cairo_set_source(cr, o->cairo_frozen_pat);
	else
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
	if (sel_visible) {
		cairo_rectangle(cr, 0, 0, pw, ph);
		cairo_rectangle(cr, sel_l, sel_t, sel_r - sel_l, sel_b - sel_t);
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
		cairo_fill(cr);
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
	} else {
		cairo_paint(cr);
	}

	if (region_editing(o->st)) {
		bool anno_drag = region_anno_dragging(o->st);
		cairo_save(cr);
		cairo_translate(cr, -o->go->x * S, -o->go->y * S);
		cairo_scale(cr, S, S);
		gren_anno_cache_ensure(o, dst);
		if (!o->anno_cache || o->st->drawing || anno_drag) {
			cairo_push_group(cr);
			if (o->anno_cache) {
				gren_anno_cache_paint(cr, o->anno_cache);
			} else {
				for (size_t i = 0; i < o->st->out_annos->n; i++) {
					if (anno_drag && o->st->out_annos->items[i].selected) continue;
					annotation_paint(cr, &o->st->out_annos->items[i], 1.0);
				}
			}
			if (anno_drag) {
				for (size_t i = 0; i < o->st->out_annos->n; i++)
					if (o->st->out_annos->items[i].selected)
						annotation_paint(cr, &o->st->out_annos->items[i], 1.0);
			}
			if (o->st->drawing) {
				int32_t px1 = o->st->cursor_x, py1 = o->st->cursor_y;
				region_apply_shape_snap(o->st->current_tool, o->st->shift_held,
										o->st->draw_x0, o->st->draw_y0, &px1, &py1);
				struct annotation preview = {
					.tool = o->st->current_tool,
					.x0 = o->st->draw_x0,
					.y0 = o->st->draw_y0,
					.x1 = px1,
					.y1 = py1,
					.color = o->st->current_color,
					.width = o->st->current_width,
					.font_size = ANNO_DEFAULT_FONT,
					.style = o->st->current_style,
					.points = o->st->pen_points,
					.n_points = o->st->pen_n,
				};
				annotation_paint(cr, &preview, 1.0);
			}
			cairo_pop_group_to_source(cr);
			cairo_paint(cr);
		} else {
			gren_anno_cache_paint(cr, o->anno_cache);
		}
		if (o->st->anno_edit_mode) gren_paint_anno_selection(cr, o->st);
		if (o->st->text_input_active) {
			double font = o->st->current_font;
			cairo_select_font_face(cr, "sans-serif",
								   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, font);
			cairo_font_extents_t fe;
			cairo_font_extents(cr, &fe);
			cairo_text_extents_t typed_ext = {0};
			if (o->st->text_len > 0) {
				cairo_text_extents(cr, o->st->text_buf, &typed_ext);
			}
			double pad = 4.0;
			double tw = typed_ext.x_advance > 0 ? typed_ext.x_advance : font * 0.6;
			double pill_w = tw + 2 * pad;
			double pill_top = (double)o->st->text_y - fe.ascent - pad;
			double pill_h = fe.ascent + fe.descent + 2 * pad;
			cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
			cairo_rectangle(cr, (double)o->st->text_x - pad, pill_top, pill_w, pill_h);
			cairo_fill(cr);
			if (o->st->text_len > 0) {
				struct annotation preview = {
					.tool = TOOL_TEXT,
					.x0 = o->st->text_x,
					.y0 = o->st->text_y,
					.color = o->st->current_color,
					.font_size = (int32_t)font,
					.text = (char *)o->st->text_buf,
				};
				annotation_paint(cr, &preview, 1.0);
			}

			double cursor_x = (double)o->st->text_x + typed_ext.x_advance;
			cairo_set_source_rgba(cr, 1.0, 0.18, 0.18, 1.0);
			cairo_set_line_width(cr, 1.5);
			cairo_move_to(cr, cursor_x, (double)o->st->text_y - fe.ascent);
			cairo_line_to(cr, cursor_x, (double)o->st->text_y + fe.descent);
			cairo_stroke(cr);
		}
		cairo_restore(cr);
	}

	if (sel_visible) {
		cairo_set_source_rgba(cr, 1, 1, 1, draw_is_snap ? 0.45 : 0.9);
		cairo_set_line_width(cr, (double)S);
		double dashes[2] = {4.0 * S, 4.0 * S};
		cairo_set_dash(cr, dashes, 2, 0);
		double half = (double)S * 0.5;
		cairo_rectangle(cr, (double)sel_l + half, (double)sel_t + half,
						(double)(sel_r - sel_l) - (double)S,
						(double)(sel_b - sel_t) - (double)S);
		cairo_stroke(cr);
		cairo_set_dash(cr, NULL, 0, 0);

		if (!draw_is_snap) {
			char dims[32];
			snprintf(dims, sizeof dims, "%dx%d", draw_w, draw_h);
			cairo_select_font_face(cr, "sans-serif",
								   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, 14.0 * S);
			cairo_text_extents_t ext;
			cairo_text_extents(cr, dims, &ext);
			double tx = (double)sel_r - ext.width - 14.0 * S;
			double ty = (double)sel_b - 14.0 * S;
			cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
			cairo_rectangle(cr, tx - 4.0 * S, ty - ext.height - 2.0 * S,
							ext.width + 8.0 * S, ext.height + 6.0 * S);
			cairo_fill(cr);
			cairo_set_source_rgba(cr, 1, 1, 1, 1);
			cairo_move_to(cr, tx, ty);
			cairo_show_text(cr, dims);
		}
	}

	if (o->st->region_locked) {
		if (region_editing(o->st) && o->st->text_input_active &&
			rect_contains((struct rect){o->go->x, o->go->y,
										o->go->logical_width, o->go->logical_height},
						  o->st->text_x, o->st->text_y)) {
			gren_render_bottom_hint(cr, o,
									o->st->text_len > 0
										? "type more, enter to commit, esc to cancel"
										: "type your text, enter to commit, esc to cancel");
		}

		if (o->st->has_selection) {
			int32_t hx[8], hy[8];
			region_handle_points(o->st, hx, hy);
			for (int i = 0; i < 8; i++) {
				hx[i] = (hx[i] - o->go->x) * S;
				hy[i] = (hy[i] - o->go->y) * S;
			}
			double hr = 6.0 * S;
			for (int i = 0; i < 8; i++) {
				cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
				cairo_arc(cr, hx[i], hy[i], hr, 0, 2.0 * M_PI);
				cairo_fill(cr);
				cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
				cairo_set_line_width(cr, 1.5 * S);
				cairo_arc(cr, hx[i], hy[i], hr, 0, 2.0 * M_PI);
				cairo_stroke(cr);
			}
		}

		if (!region_editing(o->st) && sel_visible) {
			gren_render_bottom_hint(cr, o, "enter or ctrl+c to capture, esc to cancel");
		}
	}

	if (region_editing(o->st)) {
		region_toolbar_render(cr, o);
		region_color_picker_render(cr, o);
		region_tool_picker_render(cr, o);
		region_toolbar_tooltip_render(cr, o);
	}

	if (region_magnifier_active(o)) region_magnifier_render(cr, o);
	if (region_coords_active(o)) region_coords_render(cr, o);

	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);

	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &gren_frame_listener_g, o);

	grabit_shm_slot_attach(o->surface, slot);
	wl_surface_damage_buffer(o->surface, 0, 0, pw, ph);
	wl_surface_commit(o->surface);
}
