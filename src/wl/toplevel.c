// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wl/toplevel.h"

#include "log.h"
#include "wl/wl.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

struct tl_entry {
	struct zwlr_foreign_toplevel_handle_v1 *handle;
	char *app_id;
	char *title;
	bool activated;
	struct tl_entry *next;
};

static void set_str(char **dst, const char *src) {
	free(*dst);
	*dst = src ? strdup(src) : NULL;
}

static void tl_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
					 const char *title) {
	(void)h;
	set_str(&((struct tl_entry *)data)->title, title);
}

static void tl_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
					  const char *app_id) {
	(void)h;
	set_str(&((struct tl_entry *)data)->app_id, app_id);
}

static void tl_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
							struct wl_output *o) {
	(void)data;
	(void)h;
	(void)o;
}

static void tl_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
							struct wl_output *o) {
	(void)data;
	(void)h;
	(void)o;
}

static void tl_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
					 struct wl_array *state) {
	(void)h;
	struct tl_entry *e = data;
	e->activated = false;
	uint32_t *s;
	wl_array_for_each(s, state) {
		if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) e->activated = true;
	}
}

static void tl_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
	(void)data;
	(void)h;
}

static void tl_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
	(void)h;
	((struct tl_entry *)data)->activated = false;
}

static void tl_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
					  struct zwlr_foreign_toplevel_handle_v1 *parent) {
	(void)data;
	(void)h;
	(void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	.title = tl_title,
	.app_id = tl_app_id,
	.output_enter = tl_output_enter,
	.output_leave = tl_output_leave,
	.state = tl_state,
	.done = tl_done,
	.closed = tl_closed,
	.parent = tl_parent,
};

static void mgr_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
						 struct zwlr_foreign_toplevel_handle_v1 *h) {
	(void)m;
	struct tl_entry **list = data;
	struct tl_entry *e = calloc(1, sizeof *e);
	if (!e) {
		zwlr_foreign_toplevel_handle_v1_destroy(h);
		return;
	}
	e->handle = h;
	e->next = *list;
	*list = e;
	zwlr_foreign_toplevel_handle_v1_add_listener(h, &handle_listener, e);
}

static void mgr_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m) {
	(void)data;
	(void)m;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
	.toplevel = mgr_toplevel,
	.finished = mgr_finished,
};

static void list_free(struct tl_entry *e) {
	while (e) {
		struct tl_entry *next = e->next;
		zwlr_foreign_toplevel_handle_v1_destroy(e->handle);
		free(e->app_id);
		free(e->title);
		free(e);
		e = next;
	}
}

int grabit_wl_active_toplevel(char **app_id_out, char **title_out) {
	if (app_id_out) *app_id_out = NULL;
	if (title_out) *title_out = NULL;

	struct grabit_wl_state s;
	if (grabit_wl_probe(&s) != 0) return -1;

	int rc = -1;
	struct tl_entry *list = NULL;
	struct zwlr_foreign_toplevel_manager_v1 *mgr = NULL;

	if (!s.toplevel_manager_version) {
		log_debug("toplevel: no zwlr_foreign_toplevel_manager_v1");
	} else {
		mgr = wl_registry_bind(s.registry, s.toplevel_manager_name,
							   &zwlr_foreign_toplevel_manager_v1_interface,
							   s.toplevel_manager_version);
	}

	if (mgr) {
		zwlr_foreign_toplevel_manager_v1_add_listener(mgr, &manager_listener, &list);
		wl_display_roundtrip(s.display);
		wl_display_roundtrip(s.display);

		for (struct tl_entry *e = list; e; e = e->next) {
			if (!e->activated) continue;
			if (app_id_out) set_str(app_id_out, e->app_id);
			if (title_out) set_str(title_out, e->title);
			rc = 0;
			break;
		}
	}

	list_free(list);
	if (mgr) {
		zwlr_foreign_toplevel_manager_v1_stop(mgr);
		wl_proxy_destroy((struct wl_proxy *)mgr);
	}
	grabit_wl_finish(&s);
	return rc;
}
