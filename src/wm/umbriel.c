// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/umbriel.h"

#include "log.h"
#include "util/json_path.h"
#include "util/rect.h"
#include "util/util.h"
#include "wm/ipc.h"
#include "wm/wm.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <json-c/json.h>

static char *socket_path(void) {
	const char *sock = getenv("UMBRIEL_SOCKET");
	if (sock && sock[0]) return strdup(sock);
	const char *run = getenv("XDG_RUNTIME_DIR");
	const char *wd = getenv("WAYLAND_DISPLAY");
	if (!run || !run[0] || !wd || !wd[0] || strchr(wd, '/')) return NULL;
	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/umbriel-%s.sock", run, wd) != 0) return NULL;
	return path;
}

bool grabit_umbriel_present(void) {
	char *path = socket_path();
	bool ok = path && access(path, F_OK) == 0;
	free(path);
	return ok;
}

static int request(const char *cmd, struct json_object **root_out,
				   struct json_object **arr_out) {
	*root_out = NULL;
	*arr_out = NULL;
	char *path = socket_path();
	char *req = NULL;
	if (!path || grabit_xasprintf(&req, "{\"cmd\":\"%s\"}\n", cmd) != 0) {
		free(path);
		return -1;
	}
	int rc = gwm_ipc_query(path, req, root_out);
	free(req);
	free(path);
	if (rc != 0) return -1;

	struct json_object *ok = NULL;
	if (!json_object_object_get_ex(*root_out, "ok", &ok) ||
		json_object_get_type(ok) != json_type_array) {
		struct json_object *err = NULL;
		if (json_object_object_get_ex(*root_out, "err", &err))
			log_debug("umbriel: %s", json_object_get_string(err));
		json_object_put(*root_out);
		*root_out = NULL;
		return -1;
	}
	*arr_out = ok;
	return 0;
}

static bool flag(struct json_object *obj, const char *key) {
	struct json_object *f = NULL;
	return json_object_object_get_ex(obj, key, &f) && json_object_get_boolean(f);
}

static const char *str(struct json_object *obj, const char *key) {
	struct json_object *f = NULL;
	if (!json_object_object_get_ex(obj, key, &f)) return "";
	if (json_object_get_type(f) != json_type_string) return "";
	return json_object_get_string(f);
}

static bool window_rect(struct json_object *win, struct rect *out) {
	static const char *const FIELDS[] = {"x", "y", "w", "h"};
	int32_t v[4];
	for (size_t i = 0; i < 4; i++) {
		struct json_object *f = NULL;
		if (!json_object_object_get_ex(win, FIELDS[i], &f)) return false;
		v[i] = (int32_t)json_object_get_int64(f);
	}
	if (v[2] < 1 || v[3] < 1) return false;
	*out = (struct rect){.x = v[0], .y = v[1], .w = v[2], .h = v[3]};
	return true;
}

static struct json_object *find_flagged(struct json_object *arr, const char *key) {
	size_t n = json_object_array_length(arr);
	for (size_t i = 0; i < n; i++) {
		struct json_object *o = json_object_array_get_idx(arr, i);
		if (o && json_object_get_type(o) == json_type_object && flag(o, key)) return o;
	}
	return NULL;
}

static struct json_object *fetch_flagged(const char *cmd, const char *key,
										 struct json_object **root_out) {
	struct json_object *arr = NULL;
	if (request(cmd, root_out, &arr) != 0) return NULL;
	struct json_object *o = find_flagged(arr, key);
	if (!o) {
		json_object_put(*root_out);
		*root_out = NULL;
	}
	return o;
}

int grabit_umbriel_active_window(char **class_out, char **title_out) {
	if (class_out) *class_out = NULL;
	if (title_out) *title_out = NULL;

	struct json_object *root = NULL;
	struct json_object *win = fetch_flagged("windows", "active", &root);
	if (!win) return -1;
	if (class_out) *class_out = grabit_json_get_string(win, "app_id");
	if (title_out) *title_out = grabit_json_get_string(win, "title");
	json_object_put(root);
	return 0;
}

int grabit_umbriel_active_window_rect(struct rect *out) {
	struct json_object *root = NULL;
	struct json_object *win = fetch_flagged("windows", "active", &root);
	if (!win) return -1;
	bool got = window_rect(win, out);
	json_object_put(root);
	return got ? 0 : -1;
}

static bool workspace_active(struct json_object *wss, const char *id) {
	if (!id[0]) return false;
	size_t n = json_object_array_length(wss);
	for (size_t i = 0; i < n; i++) {
		struct json_object *ws = json_object_array_get_idx(wss, i);
		if (!ws || json_object_get_type(ws) != json_type_object) continue;
		if (strcmp(str(ws, "id"), id) == 0) return flag(ws, "active");
	}
	return false;
}

static bool window_visible(struct json_object *win, struct json_object *wss) {
	if (flag(win, "tab_hidden")) return false;
	const char *ws = str(win, "workspace");
	if (ws[0]) return workspace_active(wss, ws);
	return str(win, "scratchpad")[0] && flag(win, "active");
}

int grabit_umbriel_windows(struct wm_window **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;

	int rc = -1;
	struct json_object *wroot = NULL, *wss = NULL, *root = NULL, *arr = NULL;
	if (request("workspaces", &wroot, &wss) != 0) goto out;
	if (request("windows", &root, &arr) != 0) goto out;

	size_t n = json_object_array_length(arr);
	struct wm_window *wins = calloc(n + 1, sizeof *wins);
	if (!wins) goto out;

	size_t k = 0;
	for (int pass = 0; pass < 2; pass++) {
		for (size_t i = 0; i < n; i++) {
			struct json_object *win = json_object_array_get_idx(arr, i);
			if (!win || json_object_get_type(win) != json_type_object) continue;
			if (flag(win, "floating") != (pass == 1)) continue;
			if (!window_visible(win, wss)) continue;
			if (gwm_snap_ignored(win, "app_id")) continue;
			if (!window_rect(win, &wins[k].r)) continue;
			wins[k].window_class = grabit_json_get_string(win, "app_id");
			wins[k++].title = grabit_json_get_string(win, "title");
		}
	}
	*out = wins;
	*n_out = k;
	rc = 0;

out:
	json_object_put(root);
	json_object_put(wroot);
	return rc;
}

char *grabit_umbriel_focused_output(void) {
	struct json_object *root = NULL;
	struct json_object *ws = fetch_flagged("workspaces", "focused", &root);
	if (!ws) return NULL;
	char *name = grabit_json_get_string(ws, "output");
	json_object_put(root);
	return name;
}
