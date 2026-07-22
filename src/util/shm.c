// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "util/util.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

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
