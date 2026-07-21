// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "log.h"
#include "region/annotate.h"
#include "region/wlr_input_state.h"
#include "util.h"
#include "wl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static int output_alloc_buffer(struct ro_output *o) {
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->pixel_width = o->width * o->scale;
	o->pixel_height = o->height * o->scale;

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(o->st->wls->shm, "grabit-region",
							o->pixel_width, o->pixel_height, &b) != 0) {
		return -1;
	}
	o->buffer = b.buffer;
	o->buf_data = b.map;
	o->buf_size = b.size;
	o->stride = o->pixel_width * 4;

	o->cairo_dst = grabit_cairo_image_argb(o->buf_data, o->pixel_width,
										   o->pixel_height, o->stride);
	if (!o->cairo_dst) {
		log_error("region: cairo dst surface failed");
		return -1;
	}

	const struct image *frozen = NULL;
	if (o->st->frozen) {
		const struct image *cand = &o->st->frozen[o->idx];
		if (cand->bytes && cand->width > 0 && cand->height > 0) frozen = cand;
	}
	if (frozen) {
		cairo_format_t fmt = grabit_cairo_format_for_shm(frozen->format);
		o->cairo_frozen = grabit_cairo_image(frozen->bytes, fmt,
											 frozen->width, frozen->height,
											 frozen->stride);
		if (o->cairo_frozen) {
			o->cairo_frozen_pat = cairo_pattern_create_for_surface(o->cairo_frozen);
			double psx = frozen->width > 0
							 ? (double)o->pixel_width / (double)frozen->width
							 : 1.0;
			double psy = frozen->height > 0
							 ? (double)o->pixel_height / (double)frozen->height
							 : 1.0;
			cairo_matrix_t m;
			cairo_matrix_init_scale(&m, 1.0 / psx, 1.0 / psy);
			cairo_pattern_set_matrix(o->cairo_frozen_pat, &m);
			cairo_pattern_set_filter(o->cairo_frozen_pat, CAIRO_FILTER_GOOD);
		} else {
			cairo_surface_destroy(o->cairo_frozen);
			o->cairo_frozen = NULL;
		}
	}

	wl_surface_set_buffer_scale(o->surface, o->scale);
	return 0;
}

void region_render_free_buffer(struct ro_output *o) {
	grabit_wl_callback_drop(&o->frame_cb);
	if (o->anno_cache) {
		cairo_surface_destroy(o->anno_cache);
		o->anno_cache = NULL;
	}
	if (o->cairo_frozen_pat) {
		cairo_pattern_destroy(o->cairo_frozen_pat);
		o->cairo_frozen_pat = NULL;
	}
	if (o->cairo_frozen) {
		cairo_surface_destroy(o->cairo_frozen);
		o->cairo_frozen = NULL;
	}
	if (o->cairo_dst) {
		cairo_surface_destroy(o->cairo_dst);
		o->cairo_dst = NULL;
	}
	grabit_shm_release(&o->buffer, &o->buf_data, &o->buf_size);
}

static void anno_cache_ensure(struct ro_output *o) {
	const struct annotation_list *annos = o->st->out_annos;
	int32_t S = o->scale;
	struct rect sel = {o->st->sel_x, o->st->sel_y, o->st->sel_w, o->st->sel_h};
	if (o->anno_cache &&
		(cairo_image_surface_get_width(o->anno_cache) != o->pixel_width ||
		 cairo_image_surface_get_height(o->anno_cache) != o->pixel_height)) {
		cairo_surface_destroy(o->anno_cache);
		o->anno_cache = NULL;
	}
	if (o->anno_cache && o->anno_cache_gen == annos->gen &&
		memcmp(&o->anno_cache_sel, &sel, sizeof sel) == 0)
		return;
	if (!o->anno_cache) {
		o->anno_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
												   o->pixel_width, o->pixel_height);
		if (cairo_surface_status(o->anno_cache) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(o->anno_cache);
			o->anno_cache = NULL;
			return;
		}
	}
	int32_t skip = region_anno_dragging(o->st) ? o->st->sel_anno : -1;
	cairo_t *cc = cairo_create(o->anno_cache);
	cairo_set_operator(cc, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cc);
	cairo_set_operator(cc, CAIRO_OPERATOR_OVER);
	cairo_translate(cc, -o->go->x * S, -o->go->y * S);
	cairo_scale(cc, S, S);
	for (size_t i = 0; i < annos->n; i++)
		if ((int32_t)i != skip)
			annotation_paint_backdrop(cc, &annos->items[i], 1.0, o->cairo_dst);
	cairo_destroy(cc);
	cairo_surface_flush(o->anno_cache);
	o->anno_cache_gen = annos->gen;
	o->anno_cache_sel = sel;
}

static void anno_cache_paint(cairo_t *cr, cairo_surface_t *cache) {
	cairo_save(cr);
	cairo_identity_matrix(cr);
	cairo_set_source_surface(cr, cache, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
}

static void hint_text_extents(cairo_t *cr, double S, const char *hint,
							  cairo_text_extents_t *ext) {
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 12.0 * S);
	cairo_text_extents(cr, hint, ext);
}

static void render_hint_pill(cairo_t *cr, double S, const char *hint,
							 const cairo_text_extents_t *ext, double cx, double ty,
							 double pw) {
	double pad = 8.0 * S;
	double tx = cx - ext->width / 2.0;
	if (tx < pad) tx = pad;
	if (tx + ext->width + pad > pw) tx = pw - ext->width - pad;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.78);
	cairo_rectangle(cr, tx - pad, ty - ext->height - pad,
					ext->width + pad * 2, ext->height + pad * 2);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, tx, ty);
	cairo_show_text(cr, hint);
}

static void render_bottom_hint(cairo_t *cr, const struct ro_output *o, const char *hint) {
	int32_t S = o->scale;
	cairo_text_extents_t hext;
	hint_text_extents(cr, S, hint, &hext);
	double pad = 8.0 * S;
	double ty = (double)o->pixel_height - 24.0 * S;
	if (region_editing(o->st)) {
		int32_t tbx, tby, tbw, tbh;
		region_toolbar_rect(o->st, NULL, &tbx, &tby, &tbw, &tbh);
		if (grabit_output_overlaps(o->go, (struct rect){tbx, tby, tbw, tbh})) {
			double tb_top = (double)(tby - o->go->y) * S;
			double tb_bot = (double)(tby + tbh - o->go->y) * S;
			double pill_top = ty - hext.height - pad;
			if (pill_top < tb_bot + 6.0 * S && ty + pad > tb_top - 6.0 * S)
				ty = tb_top - 6.0 * S - pad;
		}
	}
	render_hint_pill(cr, S, hint, &hext, (double)o->pixel_width / 2.0, ty,
					 (double)o->pixel_width);
}

static void paint_anno_selection(cairo_t *cr, const struct ro_state *st) {
	const struct annotation *a = region_anno_selected(st);
	if (!a) return;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	cairo_set_line_width(cr, 1.2);
	double sel_dash[2] = {4.0, 4.0};
	cairo_set_dash(cr, sel_dash, 2, 0);
	cairo_rectangle(cr, a->bbox.x - 2, a->bbox.y - 2,
					a->bbox.w + 4, a->bbox.h + 4);
	cairo_stroke(cr);
	cairo_set_dash(cr, NULL, 0, 0);
	int mask = annotation_corner_mask(a);
	for (int c = 0; c < 4; c++) {
		if (!(mask & (1 << c))) continue;
		double hx = annotation_corner_x(a, c);
		double hy = annotation_corner_y(a, c);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		cairo_rectangle(cr, hx - 4, hy - 4, 8, 8);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
		cairo_set_line_width(cr, 1.0);
		cairo_rectangle(cr, hx - 4, hy - 4, 8, 8);
		cairo_stroke(cr);
	}
}

static void output_redraw(struct ro_output *o);

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct ro_output *o = data;
	wl_callback_destroy(cb);
	o->frame_cb = NULL;
	if (o->st->cleanup) return;
	if (o->dirty) output_redraw(o);
}

static const struct wl_callback_listener frame_listener_g = {
	.done = frame_done,
};

static void output_request_redraw(struct ro_output *o) {
	o->dirty = true;
	if (o->frame_cb) return;
	output_redraw(o);
}

static void output_redraw(struct ro_output *o) {
	if (!o->configured || !o->buf_data || !o->cairo_dst) return;
	o->dirty = false;

	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;

	cairo_t *cr = cairo_create(o->cairo_dst);

	if (o->cairo_frozen_pat) {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source(cr, o->cairo_frozen_pat);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
		cairo_paint(cr);
	} else {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
		cairo_paint(cr);
	}

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

		if (sel_visible) {
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			if (o->cairo_frozen_pat) {
				cairo_set_source(cr, o->cairo_frozen_pat);
			} else {
				cairo_set_source_rgba(cr, 0, 0, 0, 0);
			}
			cairo_rectangle(cr, sel_l, sel_t, sel_r - sel_l, sel_b - sel_t);
			cairo_fill(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		}
	}

	if (region_editing(o->st)) {
		bool anno_drag = region_anno_dragging(o->st);
		cairo_save(cr);
		cairo_translate(cr, -o->go->x * S, -o->go->y * S);
		cairo_scale(cr, S, S);
		anno_cache_ensure(o);
		if (!o->anno_cache || o->st->drawing || anno_drag) {
			cairo_push_group(cr);
			if (o->anno_cache) {
				anno_cache_paint(cr, o->anno_cache);
			} else {
				for (size_t i = 0; i < o->st->out_annos->n; i++) {
					if (anno_drag && (int32_t)i == o->st->sel_anno) continue;
					annotation_paint(cr, &o->st->out_annos->items[i], 1.0);
				}
			}
			if (anno_drag) {
				const struct annotation *da = region_anno_selected(o->st);
				if (da) annotation_paint(cr, da, 1.0);
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
					.points = o->st->pen_points,
					.n_points = o->st->pen_n,
				};
				annotation_paint(cr, &preview, 1.0);
			}
			cairo_pop_group_to_source(cr);
			cairo_paint(cr);
		} else {
			anno_cache_paint(cr, o->anno_cache);
		}
		if (o->st->anno_edit_mode) paint_anno_selection(cr, o->st);
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
			render_bottom_hint(cr, o,
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
			render_bottom_hint(cr, o, "enter or ctrl+c to capture, esc to cancel");
		}
	}

	if (region_editing(o->st)) {
		region_toolbar_render(cr, o);
		region_color_picker_render(cr, o);
		region_toolbar_tooltip_render(cr, o);
	}

	cairo_destroy(cr);
	cairo_surface_flush(o->cairo_dst);

	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &frame_listener_g, o);

	wl_surface_attach(o->surface, o->buffer, 0, 0);
	wl_surface_damage_buffer(o->surface, 0, 0, pw, ph);
	wl_surface_commit(o->surface);
}

void region_render_request_redraw_all(struct ro_state *st) {
	for (size_t i = 0; i < st->n_outs; i++)
		output_request_redraw(&st->outs[i]);
}

struct ro_output *region_render_find_by_surface(struct ro_state *st, struct wl_surface *s) {
	for (size_t i = 0; i < st->n_outs; i++) {
		if (st->outs[i].surface == s) return &st->outs[i];
	}
	return NULL;
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
									uint32_t serial, uint32_t w, uint32_t h) {
	struct ro_output *o = data;
	if (o->st->cleanup) {
		zwlr_layer_surface_v1_ack_configure(ls, serial);
		return;
	}
	o->width = (int32_t)w;
	o->height = (int32_t)h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);

	region_render_free_buffer(o);
	if (output_alloc_buffer(o) != 0) {
		o->st->cancelled = true;
		o->st->finished = true;
		return;
	}
	o->configured = true;
	output_redraw(o);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct ro_output *o = data;
	o->st->cancelled = true;
	o->st->finished = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener_g = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

void region_render_attach_layer(struct ro_output *o) {
	zwlr_layer_surface_v1_add_listener(o->layer_surface,
									   &layer_surface_listener_g, o);
}
