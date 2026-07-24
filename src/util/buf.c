// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "util/util.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
