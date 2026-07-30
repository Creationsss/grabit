// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/ipc.h"

#include "log.h"
#include "util/util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <json-c/json.h>

#define WM_IPC_TIMEOUT_MS 2000
#define WM_IPC_MAX_BYTES (1 << 20)

int64_t gwm_ipc_deadline(int ms) {
	return grabit_now_ns() / 1000000 + ms;
}

static int ipc_connect(const char *path) {
	if (!path || !path[0]) return -1;

	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	size_t len = strlen(path);
	if (len >= sizeof addr.sun_path) return -1;
	memcpy(addr.sun_path, path, len + 1);

	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		close(fd);
		return -1;
	}
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	return fd;
}

static int wait_fd(int fd, short events, int64_t deadline) {
	int64_t remaining = deadline - grabit_now_ns() / 1000000;
	if (remaining <= 0) return -1;
	struct pollfd pfd = {.fd = fd, .events = events};
	return poll(&pfd, 1, (int)remaining) > 0 ? 0 : -1;
}

static int ipc_send(int fd, const char *req, int64_t deadline) {
	size_t len = strlen(req);
	while (len > 0) {
		if (wait_fd(fd, POLLOUT, deadline) != 0) return -1;
		ssize_t w = write(fd, req, len);
		if (w < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return -1;
		}
		req += w;
		len -= (size_t)w;
	}
	return 0;
}

static int slurp(int fd, struct grabit_buf *out, int64_t deadline) {
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
		if (out->len > WM_IPC_MAX_BYTES) return -1;
	}
}

int gwm_ipc_query(const char *path, const char *req,
				  struct json_object **root_out) {
	*root_out = NULL;
	int fd = ipc_connect(path);
	if (fd < 0) return -1;

	int64_t deadline = gwm_ipc_deadline(WM_IPC_TIMEOUT_MS);
	struct grabit_buf body = {0};
	int rc = ipc_send(fd, req, deadline);
	if (rc == 0) {
		shutdown(fd, SHUT_WR);
		rc = slurp(fd, &body, deadline);
	}
	close(fd);
	if (rc != 0 || !body.data) {
		grabit_buf_free(&body);
		return -1;
	}

	struct json_object *root = json_tokener_parse(body.data);
	grabit_buf_free(&body);
	if (!root) {
		log_debug("wm ipc: invalid JSON from %s", req);
		return -1;
	}
	*root_out = root;
	return 0;
}
