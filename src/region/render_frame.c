// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "region/annotate.h"
#include "region/wlr_input_state.h"
#include "ui_theme.h"
#include "util/util.h"
#include "wl/wl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "region/render_internal.h"
#include "region/window_internal.h"
#include "ui_colors.h"

#define REGION_DIM_A 0.45

static const struct grabit_output *hint_output(const struct ro_state *st) {
	if (st->win) return st->outs[0].go;
	if (!st->cursor_seen) return grabit_wl_primary_output(st->wls);
	return grabit_wl_output_at(st->wls, st->cursor_x, st->cursor_y);
}

static void window_frozen_matrix(struct ro_output *o) {
	if (!o->cairo_frozen_pat || !o->st->frozen || o->area.w <= 0 || o->area.h <= 0) return;
	const struct image *img = &o->st->frozen[o->idx];
	double rx = (double)img->width / o->area.w;
	double ry = (double)img->height / o->area.h;
	cairo_matrix_t m;
	cairo_matrix_init(&m, rx / o->vs, 0, 0, ry / o->vs, (o->vx - o->area.x) * rx,
					  (o->vy - o->area.y) * ry);
	cairo_pattern_set_matrix(o->cairo_frozen_pat, &m);
	cairo_pattern_set_filter(o->cairo_frozen_pat,
							 o->vs >= rx ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_GOOD);
}

static bool view_surface(cairo_surface_t **surf, int32_t pw, int32_t ph) {
	if (*surf && (cairo_image_surface_get_width(*surf) != pw ||
				  cairo_image_surface_get_height(*surf) != ph)) {
		cairo_surface_destroy(*surf);
		*surf = NULL;
	}
	if (*surf) return true;
	*surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	if (cairo_surface_status(*surf) == CAIRO_STATUS_SUCCESS) return false;
	cairo_surface_destroy(*surf);
	*surf = NULL;
	return false;
}

static void window_view_cache(struct ro_output *o, int32_t pw, int32_t ph,
							  int32_t box_l, int32_t box_t, int32_t box_r, int32_t box_b) {
	bool had = view_surface(&o->view_cache, pw, ph);
	if (o->view_cache &&
		!(had && o->vc_vx == o->vx && o->vc_vy == o->vy && o->vc_vs == o->vs)) {
		cairo_t *vc = cairo_create(o->view_cache);
		cairo_set_operator(vc, CAIRO_OPERATOR_CLEAR);
		cairo_paint(vc);
		cairo_set_operator(vc, CAIRO_OPERATOR_OVER);
		cairo_rectangle(vc, box_l, box_t, box_r - box_l, box_b - box_t);
		if (o->cairo_frozen_pat)
			cairo_set_source(vc, o->cairo_frozen_pat);
		else
			cairo_set_source_rgba(vc, GUI_C_SHADE, 1);
		cairo_fill(vc);
		cairo_destroy(vc);
		cairo_surface_flush(o->view_cache);
		o->vc_vx = o->vx;
		o->vc_vy = o->vy;
		o->vc_vs = o->vs;
	}

	if (!region_editing(o->st)) return;
	had = view_surface(&o->view_annos, pw, ph);
	if (!o->view_annos || (had && o->va_vx == o->vx && o->va_vy == o->vy &&
						   o->va_vs == o->vs && o->vc_anno_ver == o->anno_ver))
		return;
	cairo_t *va = cairo_create(o->view_annos);
	cairo_set_operator(va, CAIRO_OPERATOR_CLEAR);
	cairo_paint(va);
	cairo_set_operator(va, CAIRO_OPERATOR_OVER);
	if (o->anno_cache) {
		cairo_translate(va, -o->vx * o->vs, -o->vy * o->vs);
		cairo_scale(va, o->vs, o->vs);
		gren_anno_cache_paint(va, o);
	}
	cairo_destroy(va);
	cairo_surface_flush(o->view_annos);
	o->va_vx = o->vx;
	o->va_vy = o->vy;
	o->va_vs = o->vs;
	o->vc_anno_ver = o->anno_ver;
}

static bool build_preview(const struct ro_state *st, struct annotation *preview) {
	if (!st->drawing) return false;
	int32_t px1 = st->cursor_x, py1 = st->cursor_y;
	region_apply_shape_snap(st->current_tool, st->shift_held, st->draw_x0, st->draw_y0,
							&px1, &py1);
	*preview = (struct annotation){
		.tool = st->current_tool,
		.x0 = st->draw_x0,
		.y0 = st->draw_y0,
		.x1 = px1,
		.y1 = py1,
		.color = st->current_color,
		.width = st->current_width,
		.font_size = ANNO_DEFAULT_FONT,
		.style = st->current_style,
		.smooth = st->current_smooth,
		.points = st->pen_points,
		.n_points = st->pen_n,
	};
	return true;
}

struct dim_args {
	int32_t box_l, box_t, box_r, box_b;
	int32_t sel_l, sel_t, sel_r, sel_b;
	int32_t S;
	bool sel_visible;
	bool snap;
};

static void paint_dim(cairo_t *cr, const struct ro_output *o, const struct dim_args *d) {
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgba(cr, GUI_C_SHADE, REGION_DIM_A);
	if (d->sel_visible) {
		double snap_r = (d->snap && o->st->snap_radius > 0 &&
						 !region_snap_is_fullscreen(o->st))
							? (double)o->st->snap_radius * d->S
							: 0.0;
		cairo_rectangle(cr, d->box_l, d->box_t, d->box_r - d->box_l, d->box_b - d->box_t);
		grabit_cairo_rect_r(cr, d->sel_l, d->sel_t, d->sel_r - d->sel_l, d->sel_b - d->sel_t, snap_r);
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
		cairo_fill(cr);
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
		if (d->snap && o->st->snap_cur_alpha < 0.999) {
			cairo_set_source_rgba(cr, GUI_C_SHADE,
								  REGION_DIM_A * (1.0 - o->st->snap_cur_alpha));
			grabit_cairo_rect_r(cr, d->sel_l, d->sel_t, d->sel_r - d->sel_l, d->sel_b - d->sel_t,
								snap_r);
			cairo_fill(cr);
		}
	} else {
		cairo_rectangle(cr, d->box_l, d->box_t, d->box_r - d->box_l, d->box_b - d->box_t);
		cairo_fill(cr);
	}
}

void gren_output_redraw(struct ro_output *o) {
	if (!o->configured) return;
	o->dirty = false;

	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;
	const bool win = o->st->win != NULL;

	if (win) {
		region_window_view(o);
		window_frozen_matrix(o);
	} else {
		gren_overlay_view(o);
	}

	struct grabit_shm_slot *slot = grabit_shm_pool_next(
		o->st->wls->shm, "grabit-region", &o->pool, pw, ph);
	if (!slot) {
		o->dirty = true;
		return;
	}
	cairo_surface_t *dst = grabit_cairo_image_argb(slot->buf.map, pw, ph, pw * 4);
	if (!dst) return;
	cairo_t *cr = cairo_create(dst);

	int32_t box_l = 0, box_t = 0, box_r = pw, box_b = ph;
	if (win) {
		box_l = (int32_t)lround((o->area.x - o->vx) * o->vs);
		box_t = (int32_t)lround((o->area.y - o->vy) * o->vs);
		box_r = (int32_t)lround((o->area.x + o->area.w - o->vx) * o->vs);
		box_b = (int32_t)lround((o->area.y + o->area.h - o->vy) * o->vs);
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
	} else if (!o->st->region_locked && !o->st->dragging) {
		const struct rect *hl = &o->st->snap_cur;
		if (o->st->snap_cur_alpha > 0.004) {
			draw_x = hl->x;
			draw_y = hl->y;
			draw_w = hl->w;
			draw_h = hl->h;
			draw_any = true;
			draw_is_snap = true;
		}
	}
	if (draw_any) {
		int32_t sx = (int32_t)lround((draw_x - o->vx) * o->vs);
		int32_t sy = (int32_t)lround((draw_y - o->vy) * o->vs);
		int32_t sx1 = (int32_t)lround((draw_x + draw_w - o->vx) * o->vs);
		int32_t sy1 = (int32_t)lround((draw_y + draw_h - o->vy) * o->vs);
		sel_l = i32max(box_l, sx);
		sel_t = i32max(box_t, sy);
		sel_r = i32min(box_r, sx1);
		sel_b = i32min(box_b, sy1);
		sel_visible = (sel_r > sel_l && sel_b > sel_t);
	}

	if (win) {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		region_window_draw_frame(cr, o);
		cairo_save(cr);
		grabit_cairo_rect_r(cr, 0, 0, pw, ph, ui_window_panel_radius(&o->st->win->frame) * S);
		cairo_clip(cr);
		cairo_rectangle(cr, 0, UI_TITLE_H * S, pw, ph - UI_TITLE_H * S);
		cairo_clip(cr);
		if (region_editing(o->st)) {
			struct annotation sp = {0};
			bool spot = build_preview(o->st, &sp) &&
						(tool_is_layer(o->st->current_tool) ||
						 tool_samples_backdrop(o->st->current_tool));
			gren_anno_cache_ensure(o, o->cairo_frozen, spot ? &sp : NULL);
		}
		window_view_cache(o, pw, ph, box_l, box_t, box_r, box_b);
		if (o->view_cache) {
			cairo_set_source_surface(cr, o->view_cache, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
			cairo_paint(cr);
		}
	} else {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		if (o->cairo_frozen_pat)
			cairo_set_source(cr, o->cairo_frozen_pat);
		else
			cairo_set_source_rgba(cr, GUI_C_SHADE, 0);
		cairo_paint(cr);
	}

	struct dim_args dim = {box_l, box_t, box_r, box_b, sel_l, sel_t, sel_r, sel_b, S,
						   sel_visible, draw_is_snap};
	if (!win || !region_editing(o->st)) paint_dim(cr, o, &dim);

	struct annotation preview = {0};
	const struct annotation *live = NULL;
	if (region_editing(o->st)) {
		cairo_save(cr);
		cairo_translate(cr, -o->vx * o->vs, -o->vy * o->vs);
		cairo_scale(cr, o->vs, o->vs);
		build_preview(o->st, &preview);
		bool spot_preview = o->st->drawing &&
							(tool_is_layer(o->st->current_tool) ||
							 (win && tool_samples_backdrop(o->st->current_tool)));
		if (o->st->drawing && !spot_preview) live = &preview;
		if (win) {
			const struct annotation_list *al = o->st->out_annos;
			cairo_save(cr);
			cairo_rectangle(cr, o->area.x, o->area.y, o->area.w, o->area.h);
			cairo_clip(cr);
			cairo_push_group(cr);
			if (o->view_annos) {
				cairo_save(cr);
				cairo_identity_matrix(cr);
				cairo_set_source_surface(cr, o->view_annos, 0, 0);
				cairo_paint(cr);
				cairo_restore(cr);
			}
			if (region_anno_dragging(o->st))
				for (size_t i = 0; i < al->n; i++)
					if (al->items[i].selected) annotation_paint(cr, &al->items[i], 1.0);
			if (live) annotation_paint(cr, live, 1.0);
			cairo_pop_group_to_source(cr);
			cairo_paint(cr);
			cairo_restore(cr);
			cairo_save(cr);
			cairo_identity_matrix(cr);
			paint_dim(cr, o, &dim);
			cairo_restore(cr);
		} else {
			gren_anno_cache_ensure(o, dst, spot_preview ? &preview : NULL);
			gren_paint_annos(cr, o, live);
		}
		if (o->st->anno_edit_mode) gren_paint_anno_selection(cr, o->st);
		if (o->st->text_input_active) {
			double font = o->st->current_font;
			struct annotation typing = {
				.tool = o->st->text_tool,
				.x0 = o->st->text_x,
				.y0 = o->st->text_y,
				.x1 = o->st->text_ax,
				.y1 = o->st->text_ay,
				.color = o->st->current_color,
				.font_size = (int32_t)font,
				.text = (char *)o->st->text_buf,
			};
			if (o->st->text_tool == TOOL_CALLOUT) {
				annotation_paint(cr, &typing, 1.0);
			} else {
				cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
									   CAIRO_FONT_WEIGHT_BOLD);
				cairo_set_font_size(cr, font);
				cairo_font_extents_t fe;
				cairo_font_extents(cr, &fe);
				cairo_text_extents_t typed_ext = {0};
				if (o->st->text_len > 0)
					cairo_text_extents(cr, o->st->text_buf, &typed_ext);
				cairo_text_extents_t caret_ext = {0};
				size_t caret = o->st->text_caret < o->st->text_len ? o->st->text_caret
																   : o->st->text_len;
				if (caret > 0) {
					char head[sizeof o->st->text_buf];
					memcpy(head, o->st->text_buf, caret);
					head[caret] = '\0';
					cairo_text_extents(cr, head, &caret_ext);
				}
				double pad = 4.0;
				double tw = typed_ext.x_advance > 0 ? typed_ext.x_advance : font * 0.6;
				cairo_set_source_rgba(cr, GUI_C_SHADE, 0.55);
				grabit_cairo_rect_r(cr, (double)o->st->text_x - pad,
									(double)o->st->text_y - fe.ascent - pad,
									tw + 2 * pad, fe.ascent + fe.descent + 2 * pad,
									grabit_ui_radius(GUI_R_TIP));
				cairo_fill(cr);
				if (o->st->text_len > 0) annotation_paint(cr, &typing, 1.0);
				double cursor_x = (double)o->st->text_x + caret_ext.x_advance;
				cairo_set_source_rgba(cr, GUI_C_ALERT, 1.0);
				cairo_set_line_width(cr, 1.5);
				cairo_move_to(cr, cursor_x, (double)o->st->text_y - fe.ascent);
				cairo_line_to(cr, cursor_x, (double)o->st->text_y + fe.descent);
				cairo_stroke(cr);
			}
		}
		cairo_restore(cr);
	}

	if (sel_visible) {
		cairo_set_source_rgba(cr, GUI_C_INK, draw_is_snap ? 0.45 : 0.9);
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
			cairo_set_source_rgba(cr, GUI_C_SHADE, 0.7);
			grabit_cairo_rect_r(cr, tx - 4.0 * S, ty - ext.height - 2.0 * S,
								ext.width + 8.0 * S, ext.height + 6.0 * S,
								grabit_ui_radius(GUI_R_TIP) * S);
			cairo_fill(cr);
			cairo_set_source_rgba(cr, GUI_C_INK, 1);
			cairo_move_to(cr, tx, ty);
			cairo_show_text(cr, dims);
		}
	}

	char confirm_key[64], cancel_key[64];
	region_keybind_first(&o->st->keys, KA_CONFIRM, confirm_key, sizeof confirm_key);
	region_keybind_first(&o->st->keys, KA_CANCEL, cancel_key, sizeof cancel_key);
	char capture_hint[160];
	snprintf(capture_hint, sizeof capture_hint, "%s to capture, %s to cancel",
			 confirm_key[0] ? confirm_key : "enter", cancel_key[0] ? cancel_key : "esc");

	if (o->st->region_locked) {
		if (region_editing(o->st) && o->st->text_input_active &&
			(win || rect_contains((struct rect){o->go->x, o->go->y,
												o->go->logical_width, o->go->logical_height},
								  o->st->text_x, o->st->text_y))) {
			gren_render_bottom_hint(cr, o,
									o->st->text_len > 0
										? "type more, ctrl+v pastes, enter to commit, esc to cancel"
										: "type your text, ctrl+v pastes, enter to commit, esc to cancel");
		}

		if (o->st->has_selection) {
			int32_t hx[8], hy[8];
			region_handle_points(o->st, hx, hy);
			for (int i = 0; i < 8; i++) {
				hx[i] = (int32_t)lround((hx[i] - o->vx) * o->vs);
				hy[i] = (int32_t)lround((hy[i] - o->vy) * o->vs);
			}
			double hr = 6.0 * S;
			for (int i = 0; i < 8; i++) {
				cairo_set_source_rgba(cr, GUI_C_INK, 0.95);
				cairo_arc(cr, hx[i], hy[i], hr, 0, 2.0 * M_PI);
				cairo_fill(cr);
				cairo_set_source_rgba(cr, GUI_C_SHADE, 0.85);
				cairo_set_line_width(cr, 1.5 * S);
				cairo_arc(cr, hx[i], hy[i], hr, 0, 2.0 * M_PI);
				cairo_stroke(cr);
			}
		}

		if (!region_editing(o->st) && sel_visible) {
			gren_render_bottom_hint(cr, o, capture_hint);
		}
	}

	if (win) cairo_restore(cr);

	if (region_editing(o->st) && hint_output(o->st) == o->go) {
		const char *hint = NULL;
		char discard_hint[160];
		if (o->st->discard_armed) {
			snprintf(discard_hint, sizeof discard_hint,
					 "%s again or the close button discards your edits",
					 cancel_key[0] ? cancel_key : "esc");
			hint = discard_hint;
		} else if (o->st->eyedropper_mode)
			hint = "click anywhere to sample a color, esc to cancel";
		else if (!region_toolbar_visible(o->st)) {
			if (o->st->has_selection)
				hint = capture_hint;
			else if (o->st->n_snap_windows > 0)
				hint = "drag or click a window, esc to cancel";
			else
				hint = "drag to select a region, esc to cancel";
		}
		if (hint) gren_render_bottom_hint(cr, o, hint);
	}

	if (region_editing(o->st)) {
		region_toolbar_render(cr, o);
		region_color_picker_render(cr, o);
		region_tool_picker_render(cr, o);
		region_toolbar_tooltip_render(cr, o);
	}

	if (region_magnifier_active(o)) region_magnifier_render(cr, o, live);
	if (region_coords_active(o)) region_coords_render(cr, o);

	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);

	grabit_wl_callback_drop(&o->frame_cb);
	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &gren_frame_listener_g, o);

	grabit_shm_slot_attach(o->surface, slot);
	wl_surface_damage_buffer(o->surface, 0, 0, pw, ph);
	wl_surface_commit(o->surface);
}
