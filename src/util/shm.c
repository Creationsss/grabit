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

static void slot_handle_release(void *data, struct wl_buffer *buffer) {
	(void)buffer;
	struct grabit_shm_slot *slot = data;
	slot->busy = false;
}

static const struct wl_buffer_listener slot_listener_g = {
	.release = slot_handle_release,
};

struct grabit_shm_slot *grabit_shm_pool_next(struct wl_shm *shm, const char *tag,
											 struct grabit_shm_pool *p,
											 int32_t pixel_w, int32_t pixel_h) {
	for (size_t i = 0; i < sizeof p->slots / sizeof p->slots[0]; i++) {
		struct grabit_shm_slot *slot = &p->slots[i];
		if (slot->busy) continue;
		if (slot->buf.buffer && (slot->w != pixel_w || slot->h != pixel_h))
			grabit_shm_buf_destroy(&slot->buf);
		if (!slot->buf.buffer) {
			if (grabit_shm_argb_buf(shm, tag, pixel_w, pixel_h, &slot->buf) != 0)
				return NULL;
			wl_buffer_add_listener(slot->buf.buffer, &slot_listener_g, slot);
			slot->w = pixel_w;
			slot->h = pixel_h;
		}
		return slot;
	}
	return NULL;
}

void grabit_shm_pool_finish(struct grabit_shm_pool *p) {
	for (size_t i = 0; i < sizeof p->slots / sizeof p->slots[0]; i++) {
		grabit_shm_buf_destroy(&p->slots[i].buf);
		p->slots[i].busy = false;
	}
}
