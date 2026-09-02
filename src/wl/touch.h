// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WL_TOUCH_H
#define GRABIT_WL_TOUCH_H

struct wl_touch;

#include <stdbool.h>
#include <stdint.h>

struct gtouch_slot {
	int32_t id;
	bool active;
};

static inline bool gtouch_claim(struct gtouch_slot *s, int32_t id) {
	if (s->active) return false;
	s->id = id;
	s->active = true;
	return true;
}

static inline bool gtouch_owns(const struct gtouch_slot *s, int32_t id) {
	return s->active && s->id == id;
}

static inline void gtouch_clear(struct gtouch_slot *s) {
	s->active = false;
}

static inline bool gtouch_release(struct gtouch_slot *s, int32_t id) {
	if (!gtouch_owns(s, id)) return false;
	s->active = false;
	return true;
}

static inline bool gtouch_cancel(struct gtouch_slot *s) {
	bool was = s->active;
	s->active = false;
	return was;
}

static inline void gtouch_frame_noop(void *data, struct wl_touch *t) {
	(void)data;
	(void)t;
}

#endif
