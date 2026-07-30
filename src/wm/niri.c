// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/niri.h"

#include "log.h"
#include "region/region.h"
#include "util/json_path.h"
#include "util/util.h"
#include "wm/ipc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <json-c/json.h>

#define NIRI_SCREENSHOT_TIMEOUT_MS 8000

bool grabit_niri_present(void) {
	const char *sock = getenv("NIRI_SOCKET");
	return sock && sock[0];
}

static struct json_object *unwrap(struct json_object *root, const char *variant) {
	struct json_object *ok = NULL;
	if (!json_object_object_get_ex(root, "Ok", &ok)) {
		struct json_object *err = NULL;
		if (json_object_object_get_ex(root, "Err", &err))
			log_debug("niri: %s", json_object_get_string(err));
		return NULL;
	}
	if (!variant) return ok;
	struct json_object *inner = NULL;
	if (!json_object_object_get_ex(ok, variant, &inner)) return NULL;
	return json_object_get_type(inner) == json_type_null ? NULL : inner;
}

static int request(const char *req, const char *variant, enum json_type want,
				   struct json_object **root_out, struct json_object **inner_out) {
	*root_out = NULL;
	*inner_out = NULL;
	if (gwm_ipc_query(getenv("NIRI_SOCKET"), req, root_out) != 0) return -1;
	struct json_object *inner = unwrap(*root_out, variant);
	if (!inner || json_object_get_type(inner) != want) {
		json_object_put(*root_out);
		*root_out = NULL;
		return -1;
	}
	*inner_out = inner;
	return 0;
}

int grabit_niri_active_window(char **class_out, char **title_out) {
	if (class_out) *class_out = NULL;
	if (title_out) *title_out = NULL;

	struct json_object *root = NULL, *win = NULL;
	if (request("\"FocusedWindow\"\n", "FocusedWindow", json_type_object, &root, &win) != 0) return -1;
	if (class_out) *class_out = grabit_json_get_string(win, "app_id");
	if (title_out) *title_out = grabit_json_get_string(win, "title");
	json_object_put(root);
	return 0;
}

char *grabit_niri_focused_output(void) {
	struct json_object *root = NULL, *out = NULL;
	if (request("\"FocusedOutput\"\n", "FocusedOutput", json_type_object, &root, &out) != 0) return NULL;
	char *name = grabit_json_get_string(out, "name");
	json_object_put(root);
	return name;
}

struct niri_output {
	char *name;
	int32_t x, y, w, h;
};

struct niri_workspace {
	int64_t id;
	char *output;
	bool active;
};

struct niri_layout {
	struct niri_output *outs;
	size_t n_outs;
	struct niri_workspace *wss;
	size_t n_wss;
};

static void layout_free(struct niri_layout *l) {
	for (size_t i = 0; i < l->n_outs; i++)
		free(l->outs[i].name);
	for (size_t i = 0; i < l->n_wss; i++)
		free(l->wss[i].output);
	free(l->outs);
	free(l->wss);
	*l = (struct niri_layout){0};
}

static int load_outputs(struct niri_layout *l) {
	struct json_object *root = NULL, *outs = NULL;
	if (request("\"Outputs\"\n", "Outputs", json_type_object, &root, &outs) != 0)
		return -1;

	l->outs = calloc((size_t)json_object_object_length(outs) + 1, sizeof *l->outs);
	if (!l->outs) {
		json_object_put(root);
		return -1;
	}

	json_object_object_foreach(outs, key, val) {
		struct json_object *lg = NULL;
		if (!json_object_object_get_ex(val, "logical", &lg)) continue;
		if (json_object_get_type(lg) != json_type_object) continue;
		struct json_object *x = NULL, *y = NULL, *w = NULL, *h = NULL;
		if (!json_object_object_get_ex(lg, "x", &x) ||
			!json_object_object_get_ex(lg, "y", &y) ||
			!json_object_object_get_ex(lg, "width", &w) ||
			!json_object_object_get_ex(lg, "height", &h)) continue;
		struct niri_output *g = &l->outs[l->n_outs];
		g->name = strdup(key);
		if (!g->name) continue;
		g->x = (int32_t)json_object_get_int(x);
		g->y = (int32_t)json_object_get_int(y);
		g->w = (int32_t)json_object_get_int(w);
		g->h = (int32_t)json_object_get_int(h);
		l->n_outs++;
	}

	json_object_put(root);
	return 0;
}

static int load_workspaces(struct niri_layout *l) {
	struct json_object *root = NULL, *arr = NULL;
	if (request("\"Workspaces\"\n", "Workspaces", json_type_array, &root, &arr) != 0)
		return -1;

	size_t n = json_object_array_length(arr);
	l->wss = calloc(n + 1, sizeof *l->wss);
	if (!l->wss) {
		json_object_put(root);
		return -1;
	}

	for (size_t i = 0; i < n; i++) {
		struct json_object *ws = json_object_array_get_idx(arr, i);
		if (!ws || json_object_get_type(ws) != json_type_object) continue;
		struct json_object *id = NULL, *act = NULL;
		if (!json_object_object_get_ex(ws, "id", &id)) continue;
		json_object_object_get_ex(ws, "is_active", &act);
		struct niri_workspace *g = &l->wss[l->n_wss++];
		g->id = json_object_get_int64(id);
		g->output = grabit_json_get_string(ws, "output");
		g->active = act && json_object_get_boolean(act);
	}

	json_object_put(root);
	return 0;
}

static int layout_load(struct niri_layout *l) {
	*l = (struct niri_layout){0};
	if (load_outputs(l) != 0 || load_workspaces(l) != 0) {
		layout_free(l);
		return -1;
	}
	return 0;
}

static const struct niri_workspace *ws_of(const struct niri_layout *l,
										  struct json_object *win) {
	struct json_object *wid = NULL;
	if (!json_object_object_get_ex(win, "workspace_id", &wid)) return NULL;
	int64_t id = json_object_get_int64(wid);
	for (size_t i = 0; i < l->n_wss; i++)
		if (l->wss[i].id == id) return &l->wss[i];
	return NULL;
}

static const struct niri_output *out_of(const struct niri_layout *l,
										const char *name) {
	if (!name) return NULL;
	for (size_t i = 0; i < l->n_outs; i++)
		if (strcmp(l->outs[i].name, name) == 0) return &l->outs[i];
	return NULL;
}

static bool window_placed(struct json_object *win) {
	struct json_object *layout = NULL;
	if (!json_object_object_get_ex(win, "layout", &layout)) return false;
	double px, py;
	return grabit_json_pair(layout, "tile_pos_in_workspace_view", &px, &py);
}

static bool window_rect(struct json_object *win, const struct niri_layout *l,
						const struct niri_workspace *ws, struct rect *out) {
	struct json_object *layout = NULL;
	if (!json_object_object_get_ex(win, "layout", &layout)) return false;

	double px, py, ox, oy, sw, sh;
	if (!grabit_json_pair(layout, "tile_pos_in_workspace_view", &px, &py)) return false;
	if (!grabit_json_pair(layout, "window_offset_in_tile", &ox, &oy)) return false;
	if (!grabit_json_pair(layout, "window_size", &sw, &sh)) return false;
	if (sw < 1 || sh < 1) return false;

	const struct niri_output *og = out_of(l, ws ? ws->output : NULL);
	if (!og) return false;

	struct rect r = {
		.x = og->x + (int32_t)lround(px + ox),
		.y = og->y + (int32_t)lround(py + oy),
		.w = (int32_t)lround(sw),
		.h = (int32_t)lround(sh),
	};
	if (r.x < og->x || r.y < og->y ||
		r.x + r.w > og->x + og->w || r.y + r.h > og->y + og->h) {
		log_debug("niri: window rect %d,%d %dx%d outside output %s, ignoring",
				  r.x, r.y, r.w, r.h, og->name);
		return false;
	}
	*out = r;
	return true;
}

int grabit_niri_active_window_rect(struct rect *out) {
	struct json_object *root = NULL, *win = NULL;
	if (request("\"FocusedWindow\"\n", "FocusedWindow", json_type_object, &root, &win) != 0) return -1;

	int rc = -1;
	if (!window_placed(win)) {
		log_debug("niri: the focused window has no position "
				  "(niri publishes those for floating windows only)");
	} else {
		struct niri_layout l;
		if (layout_load(&l) == 0) {
			if (window_rect(win, &l, ws_of(&l, win), out)) rc = 0;
			layout_free(&l);
		}
	}

	json_object_put(root);
	return rc;
}

int grabit_niri_windows(struct rect **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;

	struct niri_layout l;
	if (layout_load(&l) != 0) return -1;

	struct json_object *root = NULL, *arr = NULL;
	if (request("\"Windows\"\n", "Windows", json_type_array, &root, &arr) != 0) {
		layout_free(&l);
		return -1;
	}

	size_t n = json_object_array_length(arr);
	struct rect *rects = calloc(n + 1, sizeof *rects);
	if (!rects) {
		json_object_put(root);
		layout_free(&l);
		return -1;
	}

	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		struct json_object *win = json_object_array_get_idx(arr, i);
		if (!win || json_object_get_type(win) != json_type_object) continue;
		const struct niri_workspace *ws = ws_of(&l, win);
		if (!ws || !ws->active) continue;
		if (window_rect(win, &l, ws, &rects[k])) k++;
	}
	*out = rects;
	*n_out = k;

	json_object_put(root);
	layout_free(&l);
	return 0;
}

static char *screenshot_request(bool cursor, const char *png_path) {
	struct json_object *p = json_object_new_string(png_path);
	if (!p) return NULL;
	char *req = NULL;
	if (grabit_xasprintf(&req,
						 "{\"Action\":{\"ScreenshotWindow\":{\"id\":null,"
						 "\"write_to_disk\":true,\"show_pointer\":%s,\"path\":%s}}}\n",
						 cursor ? "true" : "false",
						 json_object_to_json_string(p)) != 0)
		req = NULL;
	json_object_put(p);
	return req;
}

static int wait_for_file(const char *path, int64_t deadline) {
	off_t last = -1;
	int stable = 0;
	while (gwm_ipc_deadline(0) < deadline) {
		struct stat st;
		if (stat(path, &st) == 0 && st.st_size > 0) {
			if (st.st_size == last) {
				if (++stable >= 2) return 0;
			} else {
				stable = 0;
				last = st.st_size;
			}
		}
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 40000000};
		nanosleep(&ts, NULL);
	}
	return -1;
}

int grabit_niri_capture_active_window(bool cursor, const char *png_path) {
	const char *sock = getenv("NIRI_SOCKET");
	if (!sock || !sock[0] || !png_path || png_path[0] != '/') return -1;

	char *req = screenshot_request(cursor, png_path);
	if (!req) return -1;

	(void)unlink(png_path);

	struct json_object *reply = NULL;
	int rc = -1;
	if (gwm_ipc_query(sock, req, &reply) != 0) {
		log_error("niri: screenshot-window request failed");
	} else {
		if (!unwrap(reply, NULL)) {
			log_error("niri: screenshot-window action was rejected");
		} else {
			rc = wait_for_file(png_path,
							   gwm_ipc_deadline(NIRI_SCREENSHOT_TIMEOUT_MS));
			if (rc != 0)
				log_error("niri: the window screenshot never appeared");
		}
		json_object_put(reply);
	}
	free(req);
	return rc;
}
