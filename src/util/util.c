// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "util/util.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int grabit_xasprintf(char **out, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) {
		va_end(ap2);
		*out = NULL;
		return -1;
	}
	*out = malloc((size_t)n + 1);
	if (!*out) {
		va_end(ap2);
		return -1;
	}
	vsnprintf(*out, (size_t)n + 1, fmt, ap2);
	va_end(ap2);
	return 0;
}

const char *grabit_basename(const char *path) {
	if (!path) return "";
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

int grabit_read_file(const char *path, size_t max_bytes, char **out, size_t *out_len) {
	if (!path || !out || !out_len) return -1;
	*out = NULL;
	*out_len = 0;
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	long sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return -1;
	}
	if (max_bytes && (size_t)sz > max_bytes) {
		fclose(f);
		errno = EFBIG;
		return -1;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	char *buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return -1;
	}
	size_t off = 0;
	while (off < (size_t)sz) {
		size_t got = fread(buf + off, 1, (size_t)sz - off, f);
		if (got == 0) break;
		off += got;
	}
	int err = ferror(f);
	fclose(f);
	if (err || off != (size_t)sz) {
		free(buf);
		return -1;
	}
	buf[sz] = '\0';
	*out = buf;
	*out_len = (size_t)sz;
	return 0;
}

int grabit_runtime_dir(char *out, size_t cap) {
	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg && xdg[0] == '/') {
		struct stat s;
		if (stat(xdg, &s) == 0 && S_ISDIR(s.st_mode)) {
			int n = snprintf(out, cap, "%s", xdg);
			return (n > 0 && (size_t)n < cap) ? 0 : -1;
		}
	}
	char dir[64];
	int n = snprintf(dir, sizeof dir, "/tmp/grabit-%u", (unsigned)getuid());
	if (n <= 0 || (size_t)n >= sizeof dir) return -1;
	if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
	struct stat st;
	if (lstat(dir, &st) != 0) return -1;
	if (!S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
		(st.st_mode & 0777) != 0700)
		return -1;
	int m = snprintf(out, cap, "%s", dir);
	return (m > 0 && (size_t)m < cap) ? 0 : -1;
}

int grabit_write_all(int fd, const void *buf, size_t n) {
	const uint8_t *p = buf;
	while (n > 0) {
		ssize_t w = write(fd, p, n);
		if (w < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (w == 0) return -1;
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

static int hex_nybble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool grabit_parse_hex_color(const char *s, uint32_t *out) {
	if (!s || !*s) return false;
	if (*s == '#') s++;
	size_t len = strlen(s);
	uint32_t v = 0;
	if (len == 6) {
		for (int i = 0; i < 6; i++) {
			int d = hex_nybble(s[i]);
			if (d < 0) return false;
			v = (v << 4) | (uint32_t)d;
		}
		*out = v & 0xFFFFFFu;
		return true;
	}
	if (len == 3) {
		for (int i = 0; i < 3; i++) {
			int d = hex_nybble(s[i]);
			if (d < 0) return false;
			uint32_t dd = ((uint32_t)d << 4) | (uint32_t)d;
			v = (v << 8) | dd;
		}
		*out = v & 0xFFFFFFu;
		return true;
	}
	return false;
}

size_t grabit_edit_distance(const char *a, const char *b) {
	size_t la = strlen(a), lb = strlen(b);
	if (la > 64 || lb > 64) return 999;
	size_t prev[66], curr[66];
	for (size_t j = 0; j <= lb; j++)
		prev[j] = j;
	for (size_t i = 1; i <= la; i++) {
		curr[0] = i;
		for (size_t j = 1; j <= lb; j++) {
			size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
			size_t del = prev[j] + 1;
			size_t ins = curr[j - 1] + 1;
			size_t sub = prev[j - 1] + cost;
			size_t m = del < ins ? del : ins;
			if (sub < m) m = sub;
			curr[j] = m;
		}
		for (size_t j = 0; j <= lb; j++)
			prev[j] = curr[j];
	}
	return prev[lb];
}

size_t grabit_rstrip(char *s, size_t len) {
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
					   s[len - 1] == '\r' || s[len - 1] == '\n'))
		len--;
	s[len] = '\0';
	return len;
}

int64_t grabit_now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
