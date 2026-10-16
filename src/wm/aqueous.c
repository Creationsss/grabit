// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/aqueous.h"

#include "log.h"
#include "util/json_path.h"
#include "util/rect.h"
#include "util/util.h"
#include "wm/ipc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <json-c/json.h>

#define AQUEOUS_SESSION_LEN 32

static const char *socket_path(void) {
	const char *sock = getenv("AQUEOUS_SOCKET");
	return sock && sock[0] == '/' ? sock : NULL;
}

bool grabit_aqueous_present(void) {
	const char *path = socket_path();
	return path && access(path, F_OK) == 0;
}

static struct json_object *member(struct json_object *obj, const char *key,
								  enum json_type type) {
	struct json_object *f = NULL;
	if (!obj || !json_object_object_get_ex(obj, key, &f)) return NULL;
	return json_object_get_type(f) == type ? f : NULL;
}

static bool flag(struct json_object *obj, const char *key) {
	struct json_object *f = member(obj, key, json_type_boolean);
	return f && json_object_get_boolean(f);
}

static const char *str(struct json_object *obj, const char *key) {
	struct json_object *f = member(obj, key, json_type_string);
	return f ? json_object_get_string(f) : "";
}

static struct json_object *result_of(struct json_object *reply) {
	if (flag(reply, "ok")) return member(reply, "result", json_type_object);
	struct json_object *err = member(reply, "error", json_type_object);
	log_debug("aqueous: %s: %s", err ? str(err, "code") : "?",
			  err ? str(err, "message") : "malformed reply");
	return NULL;
}

static bool session_valid(const char *s) {
	return strlen(s) == AQUEOUS_SESSION_LEN &&
		   strspn(s, "0123456789abcdef") == AQUEOUS_SESSION_LEN;
}

static struct json_object *fetch_entities(struct json_object **root_out) {
	*root_out = NULL;
	const char *path = socket_path();
	if (!path) return NULL;
	int fd = gwm_ipc_open(path);
	if (fd < 0) return NULL;

	int64_t deadline = gwm_ipc_deadline(WM_IPC_TIMEOUT_MS);
	struct json_object *hello = NULL, *snap = NULL, *upsert = NULL;
	char *req = NULL;
	if (gwm_ipc_line(fd, "{\"ipc\":1,\"id\":\"1\",\"op\":\"hello\",\"params\":{}}\n",
					 deadline, &hello) != 0)
		goto out;
	const char *session = str(result_of(hello), "session");
	if (!session_valid(session)) goto out;
	if (grabit_xasprintf(&req,
						 "{\"ipc\":1,\"id\":\"2\",\"session\":\"%s\","
						 "\"op\":\"snapshot\",\"params\":{}}\n",
						 session) != 0)
		goto out;
	if (gwm_ipc_line(fd, req, deadline, &snap) != 0) goto out;
	upsert = member(member(result_of(snap), "batch", json_type_object), "upsert",
					json_type_array);
	if (upsert) {
		*root_out = snap;
		snap = NULL;
	}

out:
	if (!upsert) log_debug("aqueous: ipc snapshot unavailable");
	free(req);
	json_object_put(hello);
	json_object_put(snap);
	close(fd);
	return upsert;
}

static bool is_kind(struct json_object *e, const char *kind) {
	return e && json_object_get_type(e) == json_type_object &&
		   strcmp(str(e, "kind"), kind) == 0;
}

static struct json_object *find_entity(struct json_object *ents, const char *kind,
									   const char *id) {
	if (!id || !id[0]) return NULL;
	size_t n = json_object_array_length(ents);
	for (size_t i = 0; i < n; i++) {
		struct json_object *e = json_object_array_get_idx(ents, i);
		if (is_kind(e, kind) && strcmp(str(e, "id"), id) == 0) return e;
	}
	return NULL;
}

static bool box_of(struct json_object *win, const char *key, struct rect *out) {
	static const char *const FIELDS[] = {"x", "y", "width", "height"};
	struct json_object *box = member(win, key, json_type_object);
	int32_t v[4];
	for (size_t i = 0; i < 4; i++) {
		struct json_object *f = member(box, FIELDS[i], json_type_int);
		if (!f) return false;
		v[i] = (int32_t)json_object_get_int64(f);
	}
	if (v[2] < 1 || v[3] < 1) return false;
	*out = (struct rect){.x = v[0], .y = v[1], .w = v[2], .h = v[3]};
	return true;
}

static struct json_object *fetch_focused(struct json_object **root_out) {
	struct json_object *ents = fetch_entities(root_out);
	size_t n = ents ? json_object_array_length(ents) : 0;
	for (size_t i = 0; i < n; i++) {
		struct json_object *e = json_object_array_get_idx(ents, i);
		if (is_kind(e, "window") && flag(e, "focused") && !flag(e, "minimized"))
			return e;
	}
	json_object_put(*root_out);
	*root_out = NULL;
	return NULL;
}

int grabit_aqueous_active_window(char **class_out, char **title_out) {
	if (class_out) *class_out = NULL;
	if (title_out) *title_out = NULL;

	struct json_object *root = NULL;
	struct json_object *win = fetch_focused(&root);
	if (!win) return -1;
	if (class_out) {
		const char *app = str(win, "app_id");
		*class_out = grabit_json_get_string(win, app[0] ? "app_id" : "class");
	}
	if (title_out) *title_out = grabit_json_get_string(win, "title");
	json_object_put(root);
	return 0;
}

int grabit_aqueous_active_window_rect(struct rect *out) {
	struct json_object *root = NULL;
	struct json_object *win = fetch_focused(&root);
	if (!win) return -1;
	bool got = box_of(win, "geometry", out);
	json_object_put(root);
	return got ? 0 : -1;
}

struct border_state {
	struct rect r;
	int border;
};

static struct border_state *g_borders;
static size_t g_n_borders;
static bool g_borders_loaded;

static void load_borders(void) {
	if (g_borders_loaded) return;
	g_borders_loaded = true;
	struct json_object *root = NULL;
	struct json_object *ents = fetch_entities(&root);
	if (!ents) return;
	size_t n = json_object_array_length(ents);
	g_borders = calloc(n ? n : 1, sizeof *g_borders);
	for (size_t i = 0; g_borders && i < n; i++) {
		struct json_object *e = json_object_array_get_idx(ents, i);
		struct rect in, outer;
		if (!is_kind(e, "window") || !box_of(e, "geometry", &in) ||
			!box_of(e, "outer_geometry", &outer))
			continue;
		int32_t b = i32min(i32min(in.x - outer.x, in.y - outer.y),
						   i32min(outer.x + outer.w - in.x - in.w,
								  outer.y + outer.h - in.y - in.h));
		g_borders[g_n_borders++] = (struct border_state){.r = in, .border = b > 0 ? b : 0};
	}
	json_object_put(root);
}

int grabit_aqueous_window_border(const struct rect *win) {
	load_borders();
	for (size_t i = 0; i < g_n_borders; i++)
		if (rect_equal(g_borders[i].r, *win)) return g_borders[i].border;
	return 0;
}

static int stack_rank(struct json_object *win) {
	if (flag(win, "fullscreen")) return 4;
	if (flag(win, "always_above")) return 3;
	if (flag(win, "always_below")) return 0;
	return flag(win, "floating") ? 2 : 1;
}

int grabit_aqueous_windows(struct rect **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;

	struct json_object *root = NULL;
	struct json_object *ents = fetch_entities(&root);
	if (!ents) return -1;

	size_t n = json_object_array_length(ents);
	struct rect *rects = calloc(n + 1, sizeof *rects);
	if (!rects) {
		json_object_put(root);
		return -1;
	}

	size_t k = 0;
	for (int rank = 0; rank <= 4; rank++) {
		for (size_t i = 0; i < n; i++) {
			struct json_object *e = json_object_array_get_idx(ents, i);
			if (!is_kind(e, "window") || !flag(e, "visible") ||
				flag(e, "minimized") || stack_rank(e) != rank)
				continue;
			if (box_of(e, "geometry", &rects[k])) k++;
		}
	}
	json_object_put(root);
	*out = rects;
	*n_out = k;
	return 0;
}

static struct json_object *active_seat(struct json_object *ents) {
	struct json_object *sess = find_entity(ents, "session", "session");
	struct json_object *seat = find_entity(ents, "seat", str(sess, "default_seat"));
	if (seat) return seat;
	size_t n = json_object_array_length(ents);
	for (size_t i = 0; i < n; i++) {
		struct json_object *e = json_object_array_get_idx(ents, i);
		if (is_kind(e, "seat") && str(e, "output")[0]) return e;
	}
	return NULL;
}

char *grabit_aqueous_focused_output(void) {
	struct json_object *root = NULL;
	struct json_object *ents = fetch_entities(&root);
	if (!ents) return NULL;
	struct json_object *seat = active_seat(ents);
	struct json_object *out = find_entity(ents, "output", str(seat, "output"));
	char *name = out ? grabit_json_get_string(out, "name") : NULL;
	json_object_put(root);
	return name;
}
