// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wm/ipc.h"

#include "log.h"
#include "util/util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <json-c/json.h>

#define WM_IPC_MAX_BYTES (1 << 20)
#define WM_IPC_MAX_LINE_BYTES (5 << 20)

int64_t gwm_ipc_deadline(int ms) {
	return grabit_now_ns() / 1000000 + ms;
}

int gwm_ipc_open(const char *path) {
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
	struct pollfd pfd = {.fd = fd, .events = events};
	return grabit_poll_deadline(&pfd, 1, deadline) > 0 ? 0 : -1;
}

static int ipc_write(int fd, const void *buf, size_t len, int64_t deadline) {
	const char *p = buf;
	while (len > 0) {
		if (wait_fd(fd, POLLOUT, deadline) != 0) return -1;
		ssize_t w = write(fd, p, len);
		if (w < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return -1;
		}
		p += w;
		len -= (size_t)w;
	}
	return 0;
}

static int ipc_read_exact(int fd, void *buf, size_t len, int64_t deadline) {
	char *p = buf;
	while (len > 0) {
		if (wait_fd(fd, POLLIN, deadline) != 0) return -1;
		ssize_t r = read(fd, p, len);
		if (r == 0) return -1;
		if (r < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return -1;
		}
		p += r;
		len -= (size_t)r;
	}
	return 0;
}

static int slurp(int fd, struct grabit_buf *out, int64_t deadline, bool line) {
	size_t max = line ? WM_IPC_MAX_LINE_BYTES : WM_IPC_MAX_BYTES;
	char tmp[4096];
	for (;;) {
		if (wait_fd(fd, POLLIN, deadline) != 0) return -1;
		ssize_t r = read(fd, tmp, sizeof tmp);
		if (r == 0) return line ? -1 : 0;
		if (r < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return -1;
		}
		if (grabit_buf_putn(out, tmp, (size_t)r) != 0) return -1;
		if (line) {
			char *nl = memchr(out->data + out->len - (size_t)r, '\n', (size_t)r);
			if (nl) {
				*nl = '\0';
				return 0;
			}
		}
		if (out->len > max) return -1;
	}
}

static int parse_body(int rc, struct grabit_buf *body, const char *what,
					  struct json_object **root_out) {
	struct json_object *root = rc == 0 && body->data ? json_tokener_parse(body->data) : NULL;
	if (rc == 0 && body->data && !root) log_debug("wm ipc: invalid JSON from %s", what);
	grabit_buf_free(body);
	*root_out = root;
	return root ? 0 : -1;
}

int gwm_ipc_query(const char *path, const char *req,
				  struct json_object **root_out) {
	*root_out = NULL;
	int fd = gwm_ipc_open(path);
	if (fd < 0) return -1;

	int64_t deadline = gwm_ipc_deadline(WM_IPC_TIMEOUT_MS);
	struct grabit_buf body = {0};
	int rc = ipc_write(fd, req, strlen(req), deadline);
	if (rc == 0) {
		shutdown(fd, SHUT_WR);
		rc = slurp(fd, &body, deadline, false);
	}
	close(fd);
	return parse_body(rc, &body, req, root_out);
}

int gwm_ipc_line(int fd, const char *req, int64_t deadline,
				 struct json_object **root_out) {
	*root_out = NULL;
	struct grabit_buf body = {0};
	int rc = ipc_write(fd, req, strlen(req), deadline);
	if (rc == 0) rc = slurp(fd, &body, deadline, true);
	return parse_body(rc, &body, req, root_out);
}

#define I3_MAGIC "i3-ipc"
#define I3_MAGIC_LEN 6
#define I3_HDR_LEN (I3_MAGIC_LEN + 8)

int gwm_ipc_query_i3(const char *path, uint32_t type,
					 struct json_object **root_out) {
	*root_out = NULL;
	int fd = gwm_ipc_open(path);
	if (fd < 0) return -1;

	int64_t deadline = gwm_ipc_deadline(WM_IPC_TIMEOUT_MS);
	char hdr[I3_HDR_LEN] = {0};
	memcpy(hdr, I3_MAGIC, I3_MAGIC_LEN);
	memcpy(hdr + I3_MAGIC_LEN + 4, &type, sizeof type);

	char *body = NULL;
	uint32_t len = 0, reply = 0;
	int rc = ipc_write(fd, hdr, sizeof hdr, deadline);
	if (rc == 0) rc = ipc_read_exact(fd, hdr, sizeof hdr, deadline);
	if (rc == 0) {
		memcpy(&len, hdr + I3_MAGIC_LEN, sizeof len);
		memcpy(&reply, hdr + I3_MAGIC_LEN + 4, sizeof reply);
		if (memcmp(hdr, I3_MAGIC, I3_MAGIC_LEN) != 0 || reply != type ||
			len > WM_IPC_MAX_BYTES)
			rc = -1;
	}
	if (rc == 0) {
		body = malloc((size_t)len + 1);
		rc = body ? ipc_read_exact(fd, body, len, deadline) : -1;
	}
	close(fd);
	if (rc != 0) {
		free(body);
		return -1;
	}
	body[len] = '\0';

	struct json_object *root = json_tokener_parse(body);
	free(body);
	if (!root) {
		log_debug("wm ipc: invalid JSON from i3 message %u", type);
		return -1;
	}
	*root_out = root;
	return 0;
}
