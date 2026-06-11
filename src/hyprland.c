// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "hyprland.h"

#include "log.h"
#include "region/region.h"
#include "util.h"
#include "util/json_path.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <json-c/json.h>

static int connect_socket(void) {
	const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	if (!his || !his[0]) return -1;

	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (!xdg || !xdg[0]) return -1;

	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/hypr/%s/.socket.sock", xdg, his) != 0) return -1;

	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	if (strlen(path) >= sizeof addr.sun_path) {
		free(path);
		return -1;
	}
	memcpy(addr.sun_path, path, strlen(path) + 1);
	free(path);

	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

#define HYPR_IPC_TIMEOUT_MS 2000

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_fd(int fd, short events, int64_t deadline) {
	int64_t remaining = deadline - now_ms();
	if (remaining <= 0) return -1;
	struct pollfd pfd = {.fd = fd, .events = events};
	int pr = poll(&pfd, 1, (int)remaining);
	if (pr <= 0) return -1;
	return 0;
}

static int write_all(int fd, const char *buf, size_t len, int64_t deadline) {
	while (len > 0) {
		if (wait_fd(fd, POLLOUT, deadline) != 0) return -1;
		ssize_t w = write(fd, buf, len);
		if (w < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return -1;
		}
		buf += w;
		len -= (size_t)w;
	}
	return 0;
}

static int read_all(int fd, struct grabit_buf *out, int64_t deadline) {
	char tmp[4096];
	for (;;) {
		if (wait_fd(fd, POLLIN, deadline) != 0) return -1;
		ssize_t r = read(fd, tmp, sizeof tmp);
		if (r == 0) return 0;
		if (r < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return -1;
		}
		if (grabit_buf_putn(out, tmp, (size_t)r) != 0) return -1;
		if (out->len > 1 << 20) return -1;
	}
}

static int ipc_query(const char *cmd, struct json_object **root_out) {
	*root_out = NULL;
	int fd = connect_socket();
	if (fd < 0) return -1;
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	int64_t deadline = now_ms() + HYPR_IPC_TIMEOUT_MS;
	if (write_all(fd, cmd, strlen(cmd), deadline) != 0) {
		close(fd);
		return -1;
	}
	shutdown(fd, SHUT_WR);

	struct grabit_buf body = {0};
	int rc = read_all(fd, &body, deadline);
	close(fd);
	if (rc != 0 || !body.data) {
		grabit_buf_free(&body);
		return -1;
	}

	struct json_object *root = json_tokener_parse(body.data);
	grabit_buf_free(&body);
	if (!root) {
		log_debug("hyprland: invalid JSON from %s", cmd);
		return -1;
	}
	*root_out = root;
	return 0;
}

int grabit_hyprland_active_window(char **class_out, char **title_out) {
	if (class_out) *class_out = NULL;
	if (title_out) *title_out = NULL;

	struct json_object *root = NULL;
	if (ipc_query("j/activewindow", &root) != 0) return -1;
	if (json_object_get_type(root) != json_type_object) {
		json_object_put(root);
		return -1;
	}
	if (class_out) *class_out = grabit_json_get_string(root, "class");
	if (title_out) *title_out = grabit_json_get_string(root, "title");
	json_object_put(root);
	return 0;
}

static int collect_active_ws_ids(int64_t **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;
	struct json_object *root = NULL;
	if (ipc_query("j/monitors", &root) != 0) return -1;
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

int grabit_hyprland_clients(struct rect **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;

	int64_t *active = NULL;
	size_t n_active = 0;
	if (collect_active_ws_ids(&active, &n_active) != 0) return -1;

	struct json_object *root = NULL;
	if (ipc_query("j/clients", &root) != 0) {
		free(active);
		return -1;
	}
	if (json_object_get_type(root) != json_type_array) {
		free(active);
		json_object_put(root);
		return -1;
	}

	size_t n = json_object_array_length(root);
	struct rect *arr = calloc(n + 1, sizeof *arr);
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
		int64_t ws_id = json_object_get_int64(wid);
		if (!ws_is_active(ws_id, active, n_active)) continue;

		struct json_object *at = NULL, *sz = NULL;
		if (!json_object_object_get_ex(c, "at", &at)) continue;
		if (!json_object_object_get_ex(c, "size", &sz)) continue;
		if (json_object_get_type(at) != json_type_array ||
			json_object_get_type(sz) != json_type_array) continue;
		if (json_object_array_length(at) < 2 ||
			json_object_array_length(sz) < 2) continue;

		int32_t x = (int32_t)json_object_get_int(json_object_array_get_idx(at, 0));
		int32_t y = (int32_t)json_object_get_int(json_object_array_get_idx(at, 1));
		int32_t w = (int32_t)json_object_get_int(json_object_array_get_idx(sz, 0));
		int32_t h = (int32_t)json_object_get_int(json_object_array_get_idx(sz, 1));
		if (w <= 0 || h <= 0) continue;

		arr[k++] = (struct rect){.x = x, .y = y, .w = w, .h = h};
	}

	free(active);
	json_object_put(root);
	*out = arr;
	*n_out = k;
	return 0;
}
