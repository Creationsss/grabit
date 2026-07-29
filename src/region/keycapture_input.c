// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keybinds.h"

#include "cursor.h"
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

#include "cursor-shape-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define KC_MAX_CAPS 12

#include "region/keycapture_internal.h"

static bool is_modifier_sym(xkb_keysym_t s) {
	return (s >= XKB_KEY_Shift_L && s <= XKB_KEY_Hyper_R) ||
		   (s >= XKB_KEY_ISO_Lock && s <= XKB_KEY_ISO_Level5_Lock);
}

void gkc_join(struct kc_state *s, char *out, size_t n) {
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

void gkc_add(struct kc_state *s, const struct keybind *b) {
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

	if (grabit_shm_argb_buf(s->wls->shm, "grabit-keycapture", pw, ph, &s->buf) != 0)
		return;

	wl_surface_set_buffer_scale(s->surface, s->scale);
	wl_surface_attach(s->surface, s->buf.buffer, 0, 0);
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

const struct zwlr_layer_surface_v1_listener gkc_layer_listener = {
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
	gkc_add(s, &b);
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

const struct wl_keyboard_listener gkc_kb_listener = {
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
	if (s->cursor_shape)
		wp_cursor_shape_device_v1_set_shape(s->cursor_shape, serial,
											WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
	else if (s->cursor)
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
	gkc_add(s, &b);
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis,
					 wl_fixed_t value) {
	(void)d;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}

const struct wl_pointer_listener gkc_ptr_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
};
