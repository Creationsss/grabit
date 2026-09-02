// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WL_TOUCH_H
#define GRABIT_WL_TOUCH_H

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

#endif
