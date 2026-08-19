// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/hyprland.h"

#include "region/region.h"
#include "util/json_path.h"
#include "util/util.h"
#include "wm/ipc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>

static char *socket_path(void) {
	const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (!his || !his[0] || !xdg || !xdg[0]) return NULL;
	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/hypr/%s/.socket.sock", xdg, his) != 0) return NULL;
	return path;
}

bool grabit_hyprland_present(void) {
	char *path = socket_path();
	if (!path) return false;
	free(path);
	return true;
}

static int query(const char *cmd, struct json_object **root_out) {
	char *path = socket_path();
	if (!path) return -1;
	int rc = gwm_ipc_query(path, cmd, root_out);
	free(path);
	return rc;
}

static int query_object(const char *cmd, struct json_object **root_out) {
	if (query(cmd, root_out) != 0) return -1;
	if (json_object_get_type(*root_out) == json_type_object) return 0;
	json_object_put(*root_out);
	*root_out = NULL;
	return -1;
}

static bool client_rect(struct json_object *c, struct rect *out) {
	double x, y, w, h;
	if (!grabit_json_pair(c, "at", &x, &y)) return false;
	if (!grabit_json_pair(c, "size", &w, &h)) return false;
	if (w < 1 || h < 1) return false;
	*out = (struct rect){.x = (int32_t)lround(x), .y = (int32_t)lround(y), .w = (int32_t)lround(w), .h = (int32_t)lround(h)};
	return true;
}

int grabit_hyprland_global_rounding(void) {
	struct json_object *root = NULL;
	if (query_object("j/getoption decoration:rounding", &root) != 0) return 0;
	struct json_object *val = NULL;
	int radius = 0;
	if (json_object_object_get_ex(root, "int", &val)) {
		radius = json_object_get_int(val);
	}
	json_object_put(root);
	return radius >= 0 ? radius : 0;
}

int grabit_hyprland_global_border_size(void) {
	struct json_object *root = NULL;
	if (query_object("j/getoption general:border_size", &root) != 0) return 0;
	struct json_object *val = NULL;
	int border = 0;
	if (json_object_object_get_ex(root, "int", &val)) {
		border = json_object_get_int(val);
	}
	json_object_put(root);
	return border >= 0 ? border : 0;
}

static int32_t client_radius(struct json_object *c, int32_t def_rounding) {
	struct json_object *fs = NULL;
	if (json_object_object_get_ex(c, "fullscreen", &fs)) {
		if (json_object_get_type(fs) == json_type_boolean && json_object_get_boolean(fs))
			return 0;
		if (json_object_get_type(fs) == json_type_int && json_object_get_int(fs) != 0)
			return 0;
	}
	if (json_object_object_get_ex(c, "fullscreenClient", &fs) && json_object_get_int(fs) != 0)
		return 0;

	struct json_object *ro = NULL;
	if (json_object_object_get_ex(c, "rounding", &ro)) {
		int r = json_object_get_int(ro);
		if (r >= 0) return (int32_t)r;
	}
	return def_rounding;
}

static int32_t client_border_size(struct json_object *c, int32_t def_border) {
	struct json_object *fs = NULL;
	if (json_object_object_get_ex(c, "fullscreen", &fs)) {
		if (json_object_get_type(fs) == json_type_boolean && json_object_get_boolean(fs))
			return 0;
		if (json_object_get_type(fs) == json_type_int && json_object_get_int(fs) != 0)
			return 0;
	}
	if (json_object_object_get_ex(c, "fullscreenClient", &fs) && json_object_get_int(fs) != 0)
		return 0;

	struct json_object *bo = NULL;
	if (json_object_object_get_ex(c, "borderSize", &bo)) {
		int b = json_object_get_int(bo);
		if (b >= 0) return (int32_t)b;
	}
	return def_border;
}

int grabit_hyprland_active_window(char **class_out, char **title_out) {
	if (class_out) *class_out = NULL;
	if (title_out) *title_out = NULL;

	struct json_object *root = NULL;
	if (query_object("j/activewindow", &root) != 0) return -1;
	if (class_out) *class_out = grabit_json_get_string(root, "class");
	if (title_out) *title_out = grabit_json_get_string(root, "title");
	json_object_put(root);
	return 0;
}

int grabit_hyprland_active_window_rect(struct rect *out) {
	struct json_object *root = NULL;
	if (query_object("j/activewindow", &root) != 0) return -1;
	int rc = client_rect(root, out) ? 0 : -1;
	json_object_put(root);
	return rc;
}

int grabit_hyprland_active_window_radius(void) {
	struct json_object *root = NULL;
	if (query_object("j/activewindow", &root) != 0) return 0;
	int def_r = grabit_hyprland_global_rounding();
	int32_t radius = client_radius(root, def_r);
	json_object_put(root);
	return (int)radius;
}

int grabit_hyprland_active_window_border_size(void) {
	struct json_object *root = NULL;
	if (query_object("j/activewindow", &root) != 0) return 0;
	int def_b = grabit_hyprland_global_border_size();
	int32_t border = client_border_size(root, def_b);
	json_object_put(root);
	return (int)border;
}

int grabit_hyprland_cursorpos(int32_t *x_out, int32_t *y_out) {
	struct json_object *root = NULL;
	if (query_object("j/cursorpos", &root) != 0) return -1;
	struct json_object *xo = NULL, *yo = NULL;
	int rc = -1;
	if (json_object_object_get_ex(root, "x", &xo) &&
		json_object_object_get_ex(root, "y", &yo)) {
		*x_out = (int32_t)json_object_get_int(xo);
		*y_out = (int32_t)json_object_get_int(yo);
		rc = 0;
	}
	json_object_put(root);
	return rc;
}

static int collect_active_ws_ids(int64_t **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;
	struct json_object *root = NULL;
	if (query("j/monitors", &root) != 0) return -1;
	if (json_object_get_type(root) != json_type_array) {
		json_object_put(root);
		return -1;
	}
	size_t n = json_object_array_length(root);
	int64_t *ids = calloc(n * 2 + 1, sizeof *ids);
	if (!ids) {
		json_object_put(root);
		return -1;
	}
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		struct json_object *mon = json_object_array_get_idx(root, i);
		if (!mon) continue;
		static const char *const FIELDS[] = {"activeWorkspace", "specialWorkspace"};
		for (size_t f = 0; f < sizeof FIELDS / sizeof FIELDS[0]; f++) {
			struct json_object *ws = NULL;
			if (!json_object_object_get_ex(mon, FIELDS[f], &ws)) continue;
			struct json_object *idobj = NULL;
			if (!json_object_object_get_ex(ws, "id", &idobj)) continue;
			int64_t ws_id = json_object_get_int64(idobj);
			if (ws_id == 0) continue;
			ids[k++] = ws_id;
		}
	}
	json_object_put(root);
	*out = ids;
	*n_out = k;
	return 0;
}

static bool ws_is_active(int64_t ws, const int64_t *active, size_t n) {
	for (size_t i = 0; i < n; i++)
		if (active[i] == ws) return true;
	return false;
}

int grabit_hyprland_clients(struct snap_window **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;

	int64_t *active = NULL;
	size_t n_active = 0;
	if (collect_active_ws_ids(&active, &n_active) != 0) return -1;

	struct json_object *root = NULL;
	if (query("j/clients", &root) != 0) {
		free(active);
		return -1;
	}
	if (json_object_get_type(root) != json_type_array) {
		free(active);
		json_object_put(root);
		return -1;
	}

	int32_t def_r = (int32_t)grabit_hyprland_global_rounding();
	int32_t def_b = (int32_t)grabit_hyprland_global_border_size();

	size_t n = json_object_array_length(root);
	struct snap_window *arr = calloc(n + 1, sizeof *arr);
	if (!arr) {
		free(active);
		json_object_put(root);
		return -1;
	}

	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		struct json_object *c = json_object_array_get_idx(root, i);
		if (!c || json_object_get_type(c) != json_type_object) continue;

		struct json_object *o = NULL;
		if (json_object_object_get_ex(c, "hidden", &o) &&
			json_object_get_boolean(o)) continue;
		if (json_object_object_get_ex(c, "mapped", &o) &&
			!json_object_get_boolean(o)) continue;

		struct json_object *ws = NULL;
		if (!json_object_object_get_ex(c, "workspace", &ws)) continue;
		struct json_object *wid = NULL;
		if (!json_object_object_get_ex(ws, "id", &wid)) continue;
		if (!ws_is_active(json_object_get_int64(wid), active, n_active)) continue;

		if (client_rect(c, &arr[k].rect)) {
			arr[k].radius = client_radius(c, def_r);
			arr[k].border_size = client_border_size(c, def_b);
			k++;
		}
	}

	free(active);
	json_object_put(root);
	*out = arr;
	*n_out = k;
	return 0;
}
