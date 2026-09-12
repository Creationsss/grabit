// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/sway.h"

#include "util/json_path.h"
#include "util/rect.h"
#include "wm/ipc.h"

#include <stdlib.h>

#include <json-c/json.h>

#define SWAY_GET_OUTPUTS 3
#define SWAY_GET_TREE 4

bool grabit_sway_present(void) {
	const char *sock = getenv("SWAYSOCK");
	return sock && sock[0];
}

static int query(uint32_t type, enum json_type want, struct json_object **root_out) {
	if (gwm_ipc_query_i3(getenv("SWAYSOCK"), type, root_out) != 0) return -1;
	if (json_object_get_type(*root_out) == want) return 0;
	json_object_put(*root_out);
	*root_out = NULL;
	return -1;
}

static bool node_rect(struct json_object *node, struct rect *out) {
	struct json_object *r = NULL;
	if (!json_object_object_get_ex(node, "rect", &r)) return false;
	if (json_object_get_type(r) != json_type_object) return false;

	static const char *const FIELDS[] = {"x", "y", "width", "height"};
	int32_t v[4];
	for (size_t i = 0; i < 4; i++) {
		struct json_object *f = NULL;
		if (!json_object_object_get_ex(r, FIELDS[i], &f)) return false;
		v[i] = (int32_t)json_object_get_int64(f);
	}
	if (v[2] < 1 || v[3] < 1) return false;
	*out = (struct rect){.x = v[0], .y = v[1], .w = v[2], .h = v[3]};
	return true;
}

static bool node_flag(struct json_object *node, const char *key) {
	struct json_object *f = NULL;
	return json_object_object_get_ex(node, key, &f) && json_object_get_boolean(f);
}

static bool node_is_view(struct json_object *node) {
	struct json_object *pid = NULL;
	return json_object_object_get_ex(node, "pid", &pid) &&
		   json_object_get_type(pid) == json_type_int;
}

typedef bool (*node_fn)(struct json_object *node, void *ctx);

static bool walk(struct json_object *node, node_fn fn, void *ctx) {
	if (!node || json_object_get_type(node) != json_type_object) return false;
	if (fn(node, ctx)) return true;

	static const char *const KIDS[] = {"nodes", "floating_nodes"};
	for (size_t k = 0; k < sizeof KIDS / sizeof KIDS[0]; k++) {
		struct json_object *arr = NULL;
		if (!json_object_object_get_ex(node, KIDS[k], &arr)) continue;
		if (json_object_get_type(arr) != json_type_array) continue;
		size_t n = json_object_array_length(arr);
		for (size_t i = 0; i < n; i++)
			if (walk(json_object_array_get_idx(arr, i), fn, ctx)) return true;
	}
	return false;
}

static bool take_focused(struct json_object *node, void *ctx) {
	if (!node_is_view(node) || !node_flag(node, "focused")) return false;
	return node_rect(node, ctx);
}

int grabit_sway_active_window_rect(struct rect *out) {
	struct json_object *root = NULL;
	if (query(SWAY_GET_TREE, json_type_object, &root) != 0) return -1;
	bool got = walk(root, take_focused, out);
	json_object_put(root);
	return got ? 0 : -1;
}

struct collect {
	struct rect *arr;
	size_t n;
};

static bool count_node(struct json_object *node, void *ctx) {
	(void)node;
	(*(size_t *)ctx)++;
	return false;
}

static bool take_view(struct json_object *node, void *ctx) {
	struct collect *c = ctx;
	struct rect r;
	if (node_is_view(node) && node_flag(node, "visible") && node_rect(node, &r))
		c->arr[c->n++] = r;
	return false;
}

int grabit_sway_windows(struct rect **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;

	struct json_object *root = NULL;
	if (query(SWAY_GET_TREE, json_type_object, &root) != 0) return -1;

	size_t total = 0;
	walk(root, count_node, &total);

	struct collect c = {.arr = calloc(total + 1, sizeof *c.arr)};
	if (!c.arr) {
		json_object_put(root);
		return -1;
	}
	walk(root, take_view, &c);
	json_object_put(root);

	*out = c.arr;
	*n_out = c.n;
	return 0;
}

char *grabit_sway_focused_output(void) {
	struct json_object *root = NULL;
	if (query(SWAY_GET_OUTPUTS, json_type_array, &root) != 0) return NULL;

	char *name = NULL;
	size_t n = json_object_array_length(root);
	for (size_t i = 0; i < n && !name; i++) {
		struct json_object *o = json_object_array_get_idx(root, i);
		if (!o || json_object_get_type(o) != json_type_object) continue;
		if (node_flag(o, "focused")) name = grabit_json_get_string(o, "name");
	}
	json_object_put(root);
	return name;
}
