// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/region.h"

#include "config/config.h"
#include "cursor.h"
#include "hyprland.h"
#include "log.h"
#include "region/edit_persist.h"
#include "region/wlr_input_state.h"
#include "region/wlr_state.h"
#include "wl/wl.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

int region_select(struct grabit_wl_state *s, struct config *cfg,
				  const struct image *frozen,
				  bool annotate_mode, struct rect *out,
				  struct annotation_list *out_annos,
				  uint32_t *inout_color, int32_t *inout_width,
				  int32_t *inout_tool,
				  bool *out_choices_dirty, const struct rect *preset,
				  const struct rect *snap_rects, size_t n_snap_rects) {
	if (!s->layer_shell) {
		log_error("region: compositor lacks zwlr_layer_shell_v1");
		return -1;
	}
	if (!s->compositor) {
		log_error("region: compositor lacks wl_compositor (impossible?)");
		return -1;
	}
	if (!(s->seat_caps & WL_SEAT_CAPABILITY_POINTER) ||
		!(s->seat_caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		log_error("region: seat needs both pointer and keyboard");
		return -1;
	}

	struct ro_state st = {.wls = s, .frozen = frozen};
	st.annotate_mode = annotate_mode;
	st.out_annos = out_annos;
	st.current_tool = (inout_tool && *inout_tool >= 0 && *inout_tool < TOOL_COUNT)
						  ? (enum tool_kind) * inout_tool
						  : TOOL_PEN;
	st.current_line_tool =
		tool_is_line_family(st.current_tool) ? st.current_tool : TOOL_LINE;
	st.current_color = (inout_color && *inout_color) ? *inout_color : 0xff3030u;
	st.current_width = (inout_width && *inout_width) ? *inout_width : 4;
	st.current_font = ANNO_DEFAULT_FONT;
	st.handle_dragging = -1;
	st.hovered_button = -1;
	st.sel_anno = -1;
	st.anno_drag = ANNO_DRAG_NONE;
	st.outs = calloc(s->n_outputs, sizeof *st.outs);
	if (!st.outs) return -1;
	st.n_outs = s->n_outputs;

	grabit_wl_outputs_bbox(s, &st.bounds);

	st.snap_hover = -1;
	region_keymap_init(&st.keys, cfg);
	gregion_apply_config(&st, cfg, annotate_mode, s, snap_rects, n_snap_rects);

	st.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!st.xkb_ctx) {
		log_error("xkb_context_new failed");
		free(st.outs);
		free(st.snap_windows);
		return -1;
	}

	st.undo_timer_fd = annotate_mode
						   ? timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)
						   : -1;
	st.tooltip_timer_fd = annotate_mode
							  ? timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)
							  : -1;
	st.nudge_timer_fd = (annotate_mode || st.confirm_mode)
							? timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)
							: -1;

	st.pointer = wl_seat_get_pointer(s->seat);
	st.keyboard = wl_seat_get_keyboard(s->seat);
	region_input_attach(&st);

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->scale > max_scale) max_scale = s->outputs[i]->scale;
	}
	st.cursor_theme = grabit_cursor_theme_load(s->shm, max_scale);
	if (!st.cursor_theme) {
		log_warn("region: no cursor theme found; cursor may be invisible");
	} else {
		static const char *const cross_names[] = {
			"crosshair",
			"tcross",
			"cross",
			"cell",
			"left_ptr",
			"default",
			"arrow",
			NULL,
		};
		static const char *const text_names[] = {
			"text",
			"xterm",
			"ibeam",
			"left_ptr",
			NULL,
		};
		static const char *const move_names[] = {
			"fleur",
			"move",
			"grabbing",
			"grab",
			"all-scroll",
			"left_ptr",
			NULL,
		};
		static const char *const resize_names[8][4] = {
			{"nw-resize", "top_left_corner", "size_fdiag", NULL},
			{"n-resize", "top_side", "size_ver", NULL},
			{"ne-resize", "top_right_corner", "size_bdiag", NULL},
			{"e-resize", "right_side", "size_hor", NULL},
			{"se-resize", "bottom_right_corner", "size_fdiag", NULL},
			{"s-resize", "bottom_side", "size_ver", NULL},
			{"sw-resize", "bottom_left_corner", "size_bdiag", NULL},
			{"w-resize", "left_side", "size_hor", NULL},
		};
		st.cursor = grabit_cursor_load_first(st.cursor_theme, cross_names);
		st.cursor_text = grabit_cursor_load_first(st.cursor_theme, text_names);
		st.cursor_default = grabit_cursor_load_default(st.cursor_theme);
		st.cursor_move = grabit_cursor_load_first(st.cursor_theme, move_names);
		st.cursor_hand = grabit_cursor_load_hand(st.cursor_theme);
		for (size_t i = 0; i < 8; i++) {
			st.cursor_resize[i] = grabit_cursor_load_first(st.cursor_theme, resize_names[i]);
		}
		if (!st.cursor) log_warn("region: cursor theme has no usable cursor");
	}
	if (st.cursor) {
		st.cursor_surface = wl_compositor_create_surface(s->compositor);
	}

	gregion_create_surfaces(&st, s);

	if (preset && preset->w > 0 && preset->h > 0) {
		st.sel_x = preset->x;
		st.sel_y = preset->y;
		st.sel_w = preset->w;
		st.sel_h = preset->h;
		st.has_selection = true;
		st.snap_hover = -1;
		if (annotate_mode) st.region_locked = true;
	}

	while (!st.finished) {
		while (wl_display_prepare_read(s->display) != 0) {
			if (wl_display_dispatch_pending(s->display) < 0) {
				st.cancelled = true;
				goto loop_done;
			}
		}
		if (st.finished) {
			wl_display_cancel_read(s->display);
			break;
		}
		if (wl_display_flush(s->display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(s->display);
			st.cancelled = true;
			break;
		}

		struct pollfd pfds[4];
		pfds[0].fd = wl_display_get_fd(s->display);
		pfds[0].events = POLLIN;
		int nfds = 1;
		int undo_idx = -1, tip_idx = -1, nudge_idx = -1;
		if (st.undo_timer_fd >= 0) {
			pfds[nfds].fd = st.undo_timer_fd;
			pfds[nfds].events = POLLIN;
			undo_idx = nfds++;
		}
		if (st.tooltip_timer_fd >= 0) {
			pfds[nfds].fd = st.tooltip_timer_fd;
			pfds[nfds].events = POLLIN;
			tip_idx = nfds++;
		}
		if (st.nudge_timer_fd >= 0) {
			pfds[nfds].fd = st.nudge_timer_fd;
			pfds[nfds].events = POLLIN;
			nudge_idx = nfds++;
		}

		if (poll(pfds, (nfds_t)nfds, -1) < 0) {
			wl_display_cancel_read(s->display);
			if (errno == EINTR) continue;
			st.cancelled = true;
			break;
		}

		if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			wl_display_cancel_read(s->display);
			log_error("region: lost wayland connection");
			st.cancelled = true;
			break;
		}

		if (pfds[0].revents & POLLIN) {
			if (wl_display_read_events(s->display) < 0) {
				st.cancelled = true;
				break;
			}
		} else {
			wl_display_cancel_read(s->display);
		}
		if (wl_display_dispatch_pending(s->display) < 0) {
			st.cancelled = true;
			break;
		}

		if (undo_idx >= 0 && (pfds[undo_idx].revents & POLLIN)) {
			uint64_t expirations = 0;
			ssize_t r = read(st.undo_timer_fd, &expirations, sizeof expirations);
			(void)r;
			if (st.undo_held) {
				region_undo_pop(&st);
				region_render_request_redraw_all(&st);
			}
		}
		if (tip_idx >= 0 && (pfds[tip_idx].revents & POLLIN)) {
			uint64_t expirations = 0;
			ssize_t r = read(st.tooltip_timer_fd, &expirations, sizeof expirations);
			(void)r;
			if (st.hovered_button >= 0 && !st.tooltip_visible) {
				st.tooltip_visible = true;
				region_render_request_redraw_all(&st);
			}
		}
		if (nudge_idx >= 0 && (pfds[nudge_idx].revents & POLLIN)) {
			region_nudge_tick(&st);
		}
	}
loop_done:;

	int rc = -1;
	if (!st.cancelled && st.has_selection) {
		out->x = st.sel_x;
		out->y = st.sel_y;
		out->w = st.sel_w;
		out->h = st.sel_h;
		rc = 0;
	}
	if (inout_color) *inout_color = st.current_color;
	if (inout_width) *inout_width = st.current_width;
	if (inout_tool) *inout_tool = (int32_t)st.current_tool;
	if (out_choices_dirty) *out_choices_dirty = st.edit_choices_dirty;

	if (annotate_mode && cfg && st.tb_moved) {
		int32_t tx, ty, tw, th;
		const struct grabit_output *to;
		region_toolbar_rect(&st, &to, &tx, &ty, &tw, &th);
		const struct grabit_output *anchor = grabit_wl_output_at(s, tx, ty);
		if (!anchor) anchor = to;
		if (anchor && anchor->name)
			persist_toolbar_pos(cfg, anchor->name, tx - anchor->x, ty - anchor->y);
	}

	gregion_select_teardown(&st, s);

	return rc;
}
