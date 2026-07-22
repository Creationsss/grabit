// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keybinds.h"

#include "cursor.h"
#include "hyprland.h"
#include "log.h"
#include "util/util.h"
#include "wl/wl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define KC_MAX_CAPS 12

struct kc_state {
	struct grabit_wl_state *wls;
	struct grabit_output *go;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wl_buffer *buffer;
	void *buf_data;
	size_t buf_size;
	int32_t scale;
	bool mapped;

	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct wl_surface *cursor_surface;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *cursor;
	struct xkb_context *xkb_ctx;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;

	struct keybind caps[KC_MAX_CAPS];
	size_t n_caps;
	bool done;
	bool cancelled;
};

static bool is_modifier_sym(xkb_keysym_t s) {
	return (s >= XKB_KEY_Shift_L && s <= XKB_KEY_Hyper_R) ||
		   (s >= XKB_KEY_ISO_Lock && s <= XKB_KEY_ISO_Level5_Lock);
}

static void kc_join(struct kc_state *s, char *out, size_t n) {
	out[0] = '\0';
	size_t used = 0;
	for (size_t i = 0; i < s->n_caps; i++) {
		char one[80];
		region_keybind_format(&s->caps[i], one, sizeof one);
		int w = snprintf(out + used, n - used, "%s%s", i ? ", " : "", one);
		if (w < 0 || (size_t)w >= n - used) break;
		used += (size_t)w;
	}
}

static void kc_add(struct kc_state *s, const struct keybind *b) {
	for (size_t i = 0; i < s->n_caps; i++) {
		const struct keybind *e = &s->caps[i];
		if (e->is_button == b->is_button &&
			(b->is_button ? e->button == b->button
						  : (e->sym == b->sym && e->mods == b->mods)))
			return;
	}
	if (s->n_caps >= KC_MAX_CAPS) {
		log_warn("watch: already holding %d bindings; press Enter to save", KC_MAX_CAPS);
		return;
	}
	s->caps[s->n_caps++] = *b;
	char one[80];
	region_keybind_format(b, one, sizeof one);
	log_info("  + %s", one);
}

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
							uint32_t serial, uint32_t w, uint32_t h) {
	struct kc_state *s = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (s->mapped) return;

	s->scale = s->go->scale > 0 ? s->go->scale : 1;
	int32_t pw = (int32_t)w * s->scale;
	int32_t ph = (int32_t)h * s->scale;

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(s->wls->shm, "grabit-keycapture", pw, ph, &b) != 0)
		return;
	s->buffer = b.buffer;
	s->buf_data = b.map;
	s->buf_size = b.size;

	wl_surface_set_buffer_scale(s->surface, s->scale);
	wl_surface_attach(s->surface, s->buffer, 0, 0);
	wl_surface_damage_buffer(s->surface, 0, 0, pw, ph);
	wl_surface_commit(s->surface);
	wl_display_flush(s->wls->display);
	s->mapped = true;
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct kc_state *s = data;
	s->cancelled = true;
	s->done = true;
}

static const struct zwlr_layer_surface_v1_listener layer_listener_g = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
					  int32_t fd, uint32_t size) {
	(void)kb;
	struct kc_state *s = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	region_xkb_keymap_from_fd(s->xkb_ctx, fd, size, &s->xkb_keymap, &s->xkb_state);
}

static void kb_enter(void *d, struct wl_keyboard *kb, uint32_t serial,
					 struct wl_surface *surf, struct wl_array *keys) {
	(void)d;
	(void)kb;
	(void)serial;
	(void)surf;
	(void)keys;
}

static void kb_leave(void *d, struct wl_keyboard *kb, uint32_t serial,
					 struct wl_surface *surf) {
	(void)d;
	(void)kb;
	(void)serial;
	(void)surf;
}

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial,
				   uint32_t time, uint32_t key, uint32_t state) {
	(void)kb;
	(void)serial;
	(void)time;
	struct kc_state *s = data;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !s->xkb_state) return;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(s->xkb_state, key + 8);
	if (sym == XKB_KEY_Escape) {
		s->cancelled = true;
		s->done = true;
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		s->done = true;
		return;
	}
	if (sym == XKB_KEY_NoSymbol || is_modifier_sym(sym)) return;
	struct keybind b = {
		.is_button = false, .sym = sym, .mods = region_xkb_mods(s->xkb_state)};
	kc_add(s, &b);
}

static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
						 uint32_t depressed, uint32_t latched, uint32_t locked,
						 uint32_t group) {
	(void)kb;
	(void)serial;
	struct kc_state *s = data;
	if (s->xkb_state)
		xkb_state_update_mask(s->xkb_state, depressed, latched, locked, 0, 0, group);
}

static void kb_repeat(void *d, struct wl_keyboard *kb, int32_t rate, int32_t delay) {
	(void)d;
	(void)kb;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener kb_listener_g = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_modifiers,
	.repeat_info = kb_repeat,
};

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
					  struct wl_surface *surf, wl_fixed_t sx, wl_fixed_t sy) {
	(void)surf;
	(void)sx;
	(void)sy;
	struct kc_state *s = data;
	if (s->cursor)
		grabit_cursor_apply(p, serial, s->cursor_surface, s->cursor, s->scale);
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial,
					  struct wl_surface *surf) {
	(void)d;
	(void)p;
	(void)serial;
	(void)surf;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time,
					   wl_fixed_t sx, wl_fixed_t sy) {
	(void)d;
	(void)p;
	(void)time;
	(void)sx;
	(void)sy;
}

static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial,
					   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	(void)time;
	struct kc_state *s = data;
	if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
	if (button == BTN_LEFT) {
		log_warn("watch: left mouse is reserved for draw/select; not bindable");
		return;
	}
	struct keybind b = {.is_button = true, .button = button};
	kc_add(s, &b);
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis,
					 wl_fixed_t value) {
	(void)d;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}

static const struct wl_pointer_listener ptr_listener_g = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
};

int region_keybind_watch(struct grabit_wl_state *s, const char *action_key,
						 char *out, size_t out_size) {
	if (!s->layer_shell || !s->compositor || !s->shm || !s->seat) {
		log_error("watch: compositor lacks layer-shell/seat support");
		return -1;
	}
	if (!(s->seat_caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		log_error("watch: no keyboard on seat");
		return -1;
	}

	struct kc_state st = {.wls = s, .scale = 1};

	int32_t cpx = 0, cpy = 0;
	if (grabit_hyprland_cursorpos(&cpx, &cpy) == 0) st.go = grabit_wl_output_at(s, cpx, cpy);
	if (!st.go) st.go = grabit_wl_primary_output(s);
	if (!st.go) {
		log_error("watch: no output");
		return -1;
	}

	st.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!st.xkb_ctx) return -1;

	st.surface = wl_compositor_create_surface(s->compositor);
	st.layer = grabit_wl_layer_fullscreen(
		s, st.surface, st.go->wl_output, "grabit-keycapture",
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE, &layer_listener_g, &st);
	if (!st.layer) {
		wl_surface_destroy(st.surface);
		xkb_context_unref(st.xkb_ctx);
		return -1;
	}
	wl_surface_commit(st.surface);

	st.keyboard = wl_seat_get_keyboard(s->seat);
	if (st.keyboard) wl_keyboard_add_listener(st.keyboard, &kb_listener_g, &st);
	if (s->seat_caps & WL_SEAT_CAPABILITY_POINTER) {
		st.pointer = wl_seat_get_pointer(s->seat);
		if (st.pointer) {
			wl_pointer_add_listener(st.pointer, &ptr_listener_g, &st);
			st.cursor_theme = grabit_cursor_theme_load(s->shm, st.go->scale > 0 ? st.go->scale : 1);
			if (st.cursor_theme) {
				st.cursor = grabit_cursor_load_default(st.cursor_theme);
				if (st.cursor)
					st.cursor_surface = wl_compositor_create_surface(s->compositor);
			}
		}
	}

	log_info("watching for `%s`: press keys/mouse buttons, Enter saves, Esc cancels",
			 action_key);

	while (!st.done && grabit_wl_pump(s, -1) >= 0)
		;

	int rc;
	if (st.cancelled || st.n_caps == 0) {
		rc = 1;
	} else {
		kc_join(&st, out, out_size);
		rc = 0;
	}

	if (st.pointer) wl_pointer_release(st.pointer);
	if (st.keyboard) wl_keyboard_release(st.keyboard);
	if (st.cursor_surface) wl_surface_destroy(st.cursor_surface);
	if (st.cursor_theme) wl_cursor_theme_destroy(st.cursor_theme);
	if (st.xkb_state) xkb_state_unref(st.xkb_state);
	if (st.xkb_keymap) xkb_keymap_unref(st.xkb_keymap);
	xkb_context_unref(st.xkb_ctx);
	grabit_shm_release(&st.buffer, &st.buf_data, &st.buf_size);
	zwlr_layer_surface_v1_destroy(st.layer);
	wl_surface_destroy(st.surface);
	wl_display_roundtrip(s->display);
	return rc;
}
