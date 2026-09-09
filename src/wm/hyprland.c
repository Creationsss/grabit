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

static bool client_is_fullscreen(struct json_object *c) {
	struct json_object *fs = NULL;
	if (json_object_object_get_ex(c, "fullscreen", &fs) &&
		json_object_get_int(fs) != 0)
		return true;
	if (json_object_object_get_ex(c, "fullscreenClient", &fs) &&
		json_object_get_int(fs) != 0)
		return true;
	return false;
}

static bool target_is_fullscreen(const struct rect *win) {
	struct json_object *root = NULL;
	if (query("j/clients", &root) != 0) return false;
	bool full = false;
	if (json_object_get_type(root) == json_type_array) {
		size_t n = json_object_array_length(root);
		for (size_t i = 0; i < n; i++) {
			struct json_object *c = json_object_array_get_idx(root, i);
			struct rect r;
			if (!client_rect(c, &r)) continue;
			if (r.x != win->x || r.y != win->y || r.w != win->w || r.h != win->h)
				continue;
			full = client_is_fullscreen(c);
			break;
		}
	}
	json_object_put(root);
	return full;
}

int grabit_hyprland_window_radius(const struct rect *win) {
	static int cached = -1;
	if (cached < 0) {
		struct json_object *root = NULL;
		if (query_object("j/getoption decoration:rounding", &root) != 0) return 0;
		struct json_object *val = NULL;
		cached = 0;
		if (json_object_object_get_ex(root, "int", &val))
			cached = json_object_get_int(val);
		if (cached < 0) cached = 0;
		json_object_put(root);
	}
	int radius = cached;
	if (radius <= 0) return 0;

	return (win && target_is_fullscreen(win)) ? 0 : radius;
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

struct active_ws {
	int64_t id;
	char name[64];
	bool is_special;
};

static bool ws_name_is_special(const char *n) {
	return n && (strcmp(n, "special") == 0 || strncmp(n, "special:", 8) == 0);
}

static int collect_active_ws(struct active_ws **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;
	struct json_object *root = NULL;
	if (query("j/monitors", &root) != 0) return -1;
	if (json_object_get_type(root) != json_type_array) {
		json_object_put(root);
		return -1;
	}
	size_t n = json_object_array_length(root);
	struct active_ws *wss = calloc(n * 2 + 1, sizeof *wss);
	if (!wss) {
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
			struct json_object *idobj = NULL, *nameobj = NULL;
			int64_t ws_id = 0;
			if (json_object_object_get_ex(ws, "id", &idobj))
				ws_id = json_object_get_int64(idobj);
			const char *ws_name = NULL;
			if (json_object_object_get_ex(ws, "name", &nameobj))
				ws_name = json_object_get_string(nameobj);

			if (ws_id == 0 && (!ws_name || !ws_name[0])) continue;

			wss[k].id = ws_id;
			if (ws_name) {
				size_t len = strlen(ws_name);
				if (len < sizeof wss[k].name) memcpy(wss[k].name, ws_name, len + 1);
			}
			wss[k].is_special =
				(f == 1) || (ws_id < 0) || ws_name_is_special(ws_name);
			k++;
		}
	}
	json_object_put(root);
	*out = wss;
	*n_out = k;
	return 0;
}

static bool ws_is_active(int64_t ws_id, const char *ws_name, const struct active_ws *active, size_t n, bool *out_is_special) {
	for (size_t i = 0; i < n; i++) {
		bool match = false;
		if (ws_id != 0 && active[i].id != 0 && active[i].id == ws_id)
			match = true;
		else if (ws_name && ws_name[0] && active[i].name[0] && strcmp(active[i].name, ws_name) == 0)
			match = true;

		if (match) {
			if (out_is_special) *out_is_special = active[i].is_special;
			return true;
		}
	}
	return false;
}

static bool layer_rect(struct json_object *s, struct rect *out) {
	if (!s || json_object_get_type(s) != json_type_object) return false;
	struct json_object *o = NULL;
	if (json_object_object_get_ex(s, "namespace", &o)) {
		const char *ns = json_object_get_string(o);
		if (ns && strncmp(ns, "grabit-", 7) == 0) return false;
	}
	int32_t v[4];
	static const char *const keys[4] = {"x", "y", "w", "h"};
	for (int i = 0; i < 4; i++) {
		if (!json_object_object_get_ex(s, keys[i], &o)) return false;
		v[i] = (int32_t)json_object_get_int64(o);
	}
	if (v[2] < 1 || v[3] < 1) return false;
	*out = (struct rect){.x = v[0], .y = v[1], .w = v[2], .h = v[3]};
	return true;
}

static int push_rect(struct rect **arr, size_t *n, size_t *cap, struct rect r) {
	if (*n == *cap) {
		size_t grown_cap = *cap ? *cap * 2 : 8;
		struct rect *grown = realloc(*arr, grown_cap * sizeof **arr);
		if (!grown) return -1;
		*arr = grown;
		*cap = grown_cap;
	}
	(*arr)[(*n)++] = r;
	return 0;
}

int grabit_hyprland_layers(struct rect **below_out, size_t *n_below_out,
						   struct rect **above_out, size_t *n_above_out) {
	*below_out = NULL;
	*n_below_out = 0;
	*above_out = NULL;
	*n_above_out = 0;
	struct json_object *root = NULL;
	if (query_object("j/layers", &root) != 0) return -1;

	struct rect *below = NULL, *above = NULL;
	size_t n_below = 0, cap_below = 0, n_above = 0, cap_above = 0;
	int rc = 0;

	json_object_object_foreach(root, oname, oval) {
		(void)oname;
		struct json_object *levels = NULL;
		if (!json_object_object_get_ex(oval, "levels", &levels)) continue;
		json_object_object_foreach(levels, lvl, surfaces) {
			bool over = strcmp(lvl, "2") == 0 || strcmp(lvl, "3") == 0;
			if (!over && strcmp(lvl, "1") != 0) continue;
			if (json_object_get_type(surfaces) != json_type_array) continue;
			size_t n = json_object_array_length(surfaces);
			for (size_t i = 0; i < n; i++) {
				struct rect r;
				if (!layer_rect(json_object_array_get_idx(surfaces, i), &r)) continue;
				rc = over ? push_rect(&above, &n_above, &cap_above, r)
						  : push_rect(&below, &n_below, &cap_below, r);
				if (rc != 0) goto done;
			}
		}
	}

done:
	json_object_put(root);
	if (rc != 0) {
		free(below);
		free(above);
		return -1;
	}
	*below_out = below;
	*n_below_out = n_below;
	*above_out = above;
	*n_above_out = n_above;
	return 0;
}

enum client_tier {
	TIER_TILED = 0,
	TIER_FLOATING = 1,
	TIER_FULLSCREEN = 2,
	TIER_SPECIAL_TILED = 3,
	TIER_SPECIAL_FLOATING = 4,
	TIER_PINNED = 5,
};

struct hypr_client_item {
	struct rect r;
	enum client_tier tier;
	int64_t focus_id;
	uint64_t area;
	size_t orig_idx;
};

static int client_cmp(const void *pa, const void *pb) {
	const struct hypr_client_item *a = pa;
	const struct hypr_client_item *b = pb;

	if (a->tier != b->tier)
		return (a->tier < b->tier) ? -1 : 1;

	if (a->focus_id >= 0 && b->focus_id >= 0 && a->focus_id != b->focus_id)
		return (a->focus_id > b->focus_id) ? -1 : 1;
	if (a->focus_id >= 0 && b->focus_id < 0) return 1;
	if (a->focus_id < 0 && b->focus_id >= 0) return -1;

	if (a->area != b->area)
		return (a->area > b->area) ? -1 : 1;

	if (a->orig_idx != b->orig_idx)
		return (a->orig_idx < b->orig_idx) ? -1 : 1;

	return 0;
}

int grabit_hyprland_clients(struct rect **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;

	struct active_ws *active = NULL;
	size_t n_active = 0;
	if (collect_active_ws(&active, &n_active) != 0) return -1;

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

	size_t n = json_object_array_length(root);
	struct hypr_client_item *items = calloc(n + 1, sizeof *items);
	if (!items) {
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

		bool is_pinned = false;
		if (json_object_object_get_ex(c, "pinned", &o))
			is_pinned = json_object_get_boolean(o);

		struct json_object *ws = NULL;
		int64_t wid_val = 0;
		const char *wname_val = NULL;
		if (json_object_object_get_ex(c, "workspace", &ws) &&
			json_object_get_type(ws) == json_type_object) {
			struct json_object *wid = NULL, *wname = NULL;
			if (json_object_object_get_ex(ws, "id", &wid))
				wid_val = json_object_get_int64(wid);
			if (json_object_object_get_ex(ws, "name", &wname))
				wname_val = json_object_get_string(wname);
		}

		bool ws_active_special = false;
		bool active_window = is_pinned || ws_is_active(wid_val, wname_val, active, n_active, &ws_active_special);
		if (!active_window) continue;

		struct rect r;
		if (!client_rect(c, &r)) continue;

		bool floating = false;
		if (json_object_object_get_ex(c, "floating", &o))
			floating = json_object_get_boolean(o);

		int64_t focus_id = -1;
		if (json_object_object_get_ex(c, "focusHistoryID", &o))
			focus_id = json_object_get_int64(o);

		bool is_special =
			ws_active_special || (wid_val < 0) || ws_name_is_special(wname_val);

		bool fullscreen = false;
		if (json_object_object_get_ex(c, "fullscreen", &o))
			fullscreen = json_object_get_type(o) == json_type_boolean
							 ? json_object_get_boolean(o)
							 : (json_object_get_int64(o) & 2) != 0;

		enum client_tier tier;
		if (is_pinned)
			tier = TIER_PINNED;
		else if (is_special)
			tier = floating ? TIER_SPECIAL_FLOATING : TIER_SPECIAL_TILED;
		else if (fullscreen)
			tier = TIER_FULLSCREEN;
		else if (floating)
			tier = TIER_FLOATING;
		else
			tier = TIER_TILED;

		items[k++] = (struct hypr_client_item){
			.r = r,
			.tier = tier,
			.focus_id = focus_id,
			.area = (uint64_t)r.w * (uint64_t)r.h,
			.orig_idx = i,
		};
	}

	free(active);
	json_object_put(root);

	if (k > 1)
		qsort(items, k, sizeof *items, client_cmp);

	struct rect *arr = calloc(k + 1, sizeof *arr);
	if (!arr) {
		free(items);
		return -1;
	}

	size_t u = 0;
	for (size_t i = 0; i < k; i++) {
		bool dup = false;
		for (size_t j = i + 1; j < k; j++) {
			if (rect_equal(items[i].r, items[j].r)) {
				dup = true;
				break;
			}
		}
		if (!dup) arr[u++] = items[i].r;
	}

	free(items);
	*out = arr;
	*n_out = u;
	return 0;
}
