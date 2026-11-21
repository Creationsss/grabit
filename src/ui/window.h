// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_WINDOW_H
#define GRABIT_UI_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include "cursor.h"
#include "util/util.h"

struct grabit_wl_state;
struct wl_surface;
struct wl_pointer;
struct wl_keyboard;
struct wl_callback;
struct wl_data_device;
struct wl_data_offer;
struct xdg_surface;
struct xdg_toplevel;
struct wp_viewport;
struct wp_fractional_scale_v1;

#define UI_TITLE_H 36
#define UI_BORDER 6
#define UI_MIN_W 640
#define UI_MIN_H 420

enum ui_hit {
	UI_HIT_NONE = 0,
	UI_HIT_CONTENT,
	UI_HIT_TITLE,
	UI_HIT_CLOSE,
	UI_HIT_N,
	UI_HIT_S,
	UI_HIT_E,
	UI_HIT_W,
	UI_HIT_NE,
	UI_HIT_NW,
	UI_HIT_SE,
	UI_HIT_SW,
};

enum {
	UI_TILED_LEFT = 1,
	UI_TILED_RIGHT = 2,
	UI_TILED_TOP = 4,
	UI_TILED_BOTTOM = 8,
};

struct ui_window {
	struct grabit_wl_state *wls;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *frac;

	const char *title;
	int32_t width;
	int32_t height;
	int32_t pending_w;
	int32_t pending_h;
	int32_t scale120;
	bool activated;
	bool maximized;
	bool fullscreen;
	uint8_t tiled;
	int32_t float_w;
	int32_t float_h;
	bool kb_focused;
	bool ptr_focused;
	bool configured;
	bool closed;
	bool dirty;

	int32_t opaque_w;
	int32_t opaque_h;
	int32_t opaque_r;
	struct wl_callback *frame_cb;
	struct grabit_shm_pool pool;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	int32_t repeat_rate;
	int32_t repeat_delay;
	uint32_t repeat_key;
	int64_t repeat_next_ms;
	int64_t timer_ms;
	int64_t title_click_ms;

	double cx;
	double cy;
	enum ui_hit hover;
	bool close_armed;
	enum grabit_cursor_kind content_cursor;
	uint32_t enter_serial;
	struct grabit_cursor_set cursor;

	struct wl_data_device *data_device;
	struct wl_data_offer *drag_offer;
	bool drag_inside;

	void *user;
	void (*on_draw)(struct ui_window *w, cairo_t *cr, double width, double height);
	void (*on_button)(struct ui_window *w, double x, double y, uint32_t button,
					  bool pressed);
	void (*on_timer)(struct ui_window *w);
	void (*on_motion)(struct ui_window *w, double x, double y);
	void (*on_leave)(struct ui_window *w);
	void (*on_scroll)(struct ui_window *w, double x, double y, double units);
	bool (*on_key)(struct ui_window *w, xkb_keysym_t sym, uint8_t mods);
	void (*on_focus_lost)(struct ui_window *w);
	void (*on_focus_gained)(struct ui_window *w);
	void (*on_drag)(struct ui_window *w, bool over);
	void (*on_drop)(struct ui_window *w, const char *uri_list);
};

int ui_window_open(struct ui_window *w, struct grabit_wl_state *s,
				   const char *title, int32_t width, int32_t height);
void ui_window_finish(struct ui_window *w);
void ui_window_damage(struct ui_window *w);
int ui_window_run(struct ui_window *w);

enum ui_hit ui_frame_hit(const struct ui_window *w, double x, double y);
bool ui_window_snapped(const struct ui_window *w);
double ui_window_panel_radius(const struct ui_window *w);
void ui_frame_draw(struct ui_window *w, cairo_t *cr);

void ui_input_attach(struct ui_window *w);
void ui_input_detach(struct ui_window *w);
void ui_dnd_attach(struct ui_window *w);
void ui_dnd_detach(struct ui_window *w);
void ui_window_set_timer(struct ui_window *w, int delay_ms);
int ui_input_timeout(struct ui_window *w);
void ui_input_fire_timers(struct ui_window *w);
uint8_t ui_window_mods(const struct ui_window *w);

#endif
