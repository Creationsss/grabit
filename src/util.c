// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "util.h"

#include "log.h"

#include <wayland-client.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

bool grabit_in_path(const char *bin) {
	char tmp[4096];
	return grabit_resolve_in_path(bin, tmp, sizeof tmp) == 0;
}

int grabit_resolve_in_path(const char *bin, char *out, size_t cap) {
	if (!bin || !bin[0] || !out || cap == 0) return -1;
	if (strchr(bin, '/')) {
		if (access(bin, X_OK) != 0) return -1;
		size_t n = strlen(bin);
		if (n + 1 > cap) return -1;
		memcpy(out, bin, n + 1);
		return 0;
	}
	const char *path = getenv("PATH");
	if (!path || !path[0]) return -1;
	const char *p = path;
	while (*p) {
		const char *colon = strchr(p, ':');
		size_t len = colon ? (size_t)(colon - p) : strlen(p);
		if (len > 0) {
			int n = snprintf(out, cap, "%.*s/%s", (int)len, p, bin);
			if (n > 0 && (size_t)n < cap && access(out, X_OK) == 0) {
				return 0;
			}
		}
		if (!colon) break;
		p = colon + 1;
	}
	out[0] = '\0';
	return -1;
}

int grabit_shm_anon(const char *tag, size_t size) {
	int fd = memfd_create(tag ? tag : "grabit", MFD_CLOEXEC);
	if (fd < 0) {
		log_error("memfd_create(%s): %s", tag ? tag : "grabit", strerror(errno));
		return -1;
	}
	if (ftruncate(fd, (off_t)size) < 0) {
		log_error("ftruncate(%zu): %s", size, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

int grabit_buf_grow(struct grabit_buf *b, size_t need) {
	if (b->cap >= need) return 0;
	size_t cap = b->cap ? b->cap : 256;
	while (cap < need)
		cap *= 2;
	char *p = realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

int grabit_buf_putn(struct grabit_buf *b, const void *s, size_t n) {
	if (grabit_buf_grow(b, b->len + n + 1) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

int grabit_buf_puts(struct grabit_buf *b, const char *s) {
	return grabit_buf_putn(b, s, strlen(s));
}

int grabit_buf_putc(struct grabit_buf *b, char c) {
	return grabit_buf_putn(b, &c, 1);
}

void grabit_buf_free(struct grabit_buf *b) {
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
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
	int n = snprintf(out, cap, "/tmp");
	return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

bool grabit_process_alive(pid_t pid) {
	if (pid <= 0) return false;
	if (kill(pid, 0) == 0) return true;
	return errno != ESRCH;
}

int grabit_shm_argb_buf(struct wl_shm *shm, const char *tag,
						int32_t pixel_w, int32_t pixel_h,
						struct grabit_shm_buf *out) {
	memset(out, 0, sizeof *out);
	if (pixel_w <= 0 || pixel_h <= 0 ||
		pixel_w > GRABIT_MAX_PIXEL_SIDE || pixel_h > GRABIT_MAX_PIXEL_SIDE) {
		log_error("shm buffer %dx%d out of range", pixel_w, pixel_h);
		return -1;
	}
	int32_t stride = pixel_w * 4;
	size_t size = (size_t)stride * (size_t)pixel_h;
	int fd = grabit_shm_anon(tag ? tag : "grabit", size);
	if (fd < 0) return -1;
	void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		log_error("mmap(%zu): %s", size, strerror(errno));
		close(fd);
		return -1;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, pixel_w, pixel_h,
													  stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	out->buffer = buf;
	out->map = map;
	out->size = size;
	return 0;
}

void grabit_shm_buf_destroy(struct grabit_shm_buf *b) {
	if (!b) return;
	if (b->buffer) wl_buffer_destroy(b->buffer);
	if (b->map) munmap(b->map, b->size);
	memset(b, 0, sizeof *b);
}

void grabit_shm_release(struct wl_buffer **buf, void **map, size_t *size) {
	if (buf && *buf) {
		wl_buffer_destroy(*buf);
		*buf = NULL;
	}
	if (map && *map && size) {
		munmap(*map, *size);
		*map = NULL;
		*size = 0;
	}
}

void grabit_redirect_stdio_devnull(void) {
	int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (fd < 0) return;
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	if (fd > STDERR_FILENO) close(fd);
}

int grabit_waitpid_intr(pid_t pid, int *status) {
	while (waitpid(pid, status, 0) < 0) {
		if (errno == EINTR) continue;
		return -1;
	}
	return 0;
}

int grabit_waitpid_intr_stop(pid_t pid, int *status, atomic_int *stop) {
	bool sent_stop = false;
	while (waitpid(pid, status, 0) < 0) {
		if (errno == EINTR) {
			if (!sent_stop && stop && atomic_load(stop)) {
				kill(pid, SIGINT);
				sent_stop = true;
			}
			continue;
		}
		return -1;
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

int grabit_spawn_capture(char *const argv[], bool merge_stderr, size_t max_bytes,
						 struct grabit_buf *out, bool *capped, int *status) {
	if (capped) *capped = false;
	int p[2];
	if (pipe(p) != 0) {
		log_error("%s: pipe: %s", argv[0], strerror(errno));
		return -1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		log_error("%s: fork: %s", argv[0], strerror(errno));
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		if (dup2(p[1], STDOUT_FILENO) < 0) _exit(126);
		if (merge_stderr && dup2(p[1], STDERR_FILENO) < 0) _exit(126);
		close(p[0]);
		close(p[1]);
		execvp(argv[0], argv);
		_exit(errno == ENOENT ? 127 : 126);
	}
	close(p[1]);

	char chunk[4096];
	for (;;) {
		ssize_t n = read(p[0], chunk, sizeof chunk);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		if (max_bytes > 0 && out->len + (size_t)n > max_bytes) {
			if (capped) *capped = true;
			break;
		}
		if (grabit_buf_putn(out, chunk, (size_t)n) != 0) {
			log_error("%s: oom reading output", argv[0]);
			close(p[0]);
			kill(pid, SIGTERM);
			(void)grabit_waitpid_intr(pid, NULL);
			return -1;
		}
	}
	close(p[0]);

	if (grabit_waitpid_intr(pid, status) != 0) return -1;
	if (out->data && grabit_buf_putc(out, '\0') == 0) out->len--;
	return 0;
}

bool grabit_is_grabit_process(pid_t pid) {
	if (pid <= 0) return false;
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char comm[32] = {0};
	bool ok = fgets(comm, sizeof comm, f) != NULL;
	fclose(f);
	if (!ok) return false;
	char *nl = strchr(comm, '\n');
	if (nl) *nl = '\0';
	const char *base = comm;
	if (base[0] == '.') base++;
	if (strncmp(base, "grabit", 6) != 0) return false;
	return base[6] == '\0' || base[6] == '-';
}

void grabit_install_signal_handler(int sig, void (*handler)(int)) {
	struct sigaction sa = {0};
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}

void grabit_ignore_signal(int sig) {
	struct sigaction sa = {0};
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}

void grabit_double_fork_detach(void) {
	pid_t gp = fork();
	if (gp < 0) _exit(2);
	if (gp != 0) _exit(0);
	setsid();
}

int64_t grabit_now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
