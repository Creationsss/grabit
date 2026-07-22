// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CLIPBOARD_INTERNAL_H
#define GRABIT_CLIPBOARD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

struct grabit_wl_state;

struct clip_payload {
	const void *bytes;
	size_t size;
	const char *const *mimes;
	size_t n_mimes;
};

int clipboard_send_bytes(const void *bytes, size_t size,
						 const char *const *mimes, size_t n_mimes);

struct clip_serve_state {
	const struct clip_payload *pay;
	bool cancelled;
};

void clip_write_all(int fd, const void *bytes, size_t size);
void clip_signal_ready(int *ready_fd, char status);
void clip_mute_stderr(void);

typedef int (*clip_serve_fn)(struct grabit_wl_state *s, const struct clip_payload *p,
							 int *ready_fd);

int clip_ext_serve(struct grabit_wl_state *s, const struct clip_payload *p, int *ready_fd);
int clip_wlr_serve(struct grabit_wl_state *s, const struct clip_payload *p, int *ready_fd);

#endif
