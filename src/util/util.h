// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UTIL_H
#define GRABIT_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define GRABIT_MAX_PIXEL_SIDE 16384

int grabit_xasprintf(char **out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int grabit_shm_anon(const char *tag, size_t size);

bool grabit_in_path(const char *bin);
int grabit_resolve_in_path(const char *bin, char *out, size_t cap);

int64_t grabit_now_ns(void);
void grabit_sleep_secs(int secs);

const char *grabit_basename(const char *path);

int grabit_runtime_dir(char *out, size_t cap);
int grabit_write_all(int fd, const void *buf, size_t n);
bool grabit_process_alive(pid_t pid);
int grabit_lock_acquire(const char *path);
pid_t grabit_lock_owner(const char *path);

int grabit_waitpid_intr(pid_t pid, int *status);

#include <stdatomic.h>
int grabit_waitpid_intr_stop(pid_t pid, int *status, atomic_int *stop);

size_t grabit_edit_distance(const char *a, const char *b);

size_t grabit_rstrip(char *s, size_t len);

#include <stdint.h>
bool grabit_parse_hex_color(const char *s, uint32_t *out);

struct wl_shm;
struct wl_buffer;
struct grabit_shm_buf {
	struct wl_buffer *buffer;
	void *map;
	size_t size;
};
int grabit_shm_argb_buf(struct wl_shm *shm, const char *tag,
						int32_t pixel_w, int32_t pixel_h,
						struct grabit_shm_buf *out);
void grabit_shm_buf_destroy(struct grabit_shm_buf *b);

#define GRABIT_SHM_SLOTS 2

struct grabit_shm_slot {
	struct grabit_shm_buf buf;
	int32_t w;
	int32_t h;
	bool busy;
};

struct grabit_shm_pool {
	struct grabit_shm_slot slots[GRABIT_SHM_SLOTS];
};

struct grabit_shm_slot *grabit_shm_pool_next(struct wl_shm *shm, const char *tag,
											 struct grabit_shm_pool *p,
											 int32_t pixel_w, int32_t pixel_h);
void grabit_shm_pool_finish(struct grabit_shm_pool *p);
struct wl_surface;
void grabit_shm_slot_attach(struct wl_surface *surface, struct grabit_shm_slot *slot);

void grabit_redirect_stdio_devnull(void);

bool grabit_desktop_is(const char *needle);

struct grabit_buf {
	char *data;
	size_t len;
	size_t cap;
};

int grabit_buf_grow(struct grabit_buf *b, size_t need);
int grabit_buf_putn(struct grabit_buf *b, const void *s, size_t n);
int grabit_buf_puts(struct grabit_buf *b, const char *s);
int grabit_buf_putc(struct grabit_buf *b, char c);
void grabit_buf_free(struct grabit_buf *b);

int grabit_read_file(const char *path, size_t max_bytes, char **out, size_t *out_len);

int grabit_spawn_capture(char *const argv[], bool merge_stderr, size_t max_bytes,
						 struct grabit_buf *out, bool *capped, int *status);

void grabit_install_signal_handler(int sig, void (*handler)(int));
void grabit_ignore_signal(int sig);
void grabit_double_fork_detach(void);

#endif
