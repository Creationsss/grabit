// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "log.h"
#include "upload/sxcu.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char B64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *gsxcu_base64_encode(const char *src) {
	if (!src) return strdup("");
	size_t len = strlen(src);
	size_t out_len = ((len + 2) / 3) * 4;
	char *out = malloc(out_len + 1);
	if (!out) return NULL;
	size_t i, o = 0;
	for (i = 0; i + 2 < len; i += 3) {
		uint32_t t = ((uint8_t)src[i] << 16) | ((uint8_t)src[i + 1] << 8) | (uint8_t)src[i + 2];
		out[o++] = B64[(t >> 18) & 0x3F];
		out[o++] = B64[(t >> 12) & 0x3F];
		out[o++] = B64[(t >> 6) & 0x3F];
		out[o++] = B64[t & 0x3F];
	}
	if (i < len) {
		uint32_t a = (uint8_t)src[i];
		uint32_t b = (i + 1 < len) ? (uint8_t)src[i + 1] : 0;
		uint32_t t = (a << 16) | (b << 8);
		out[o++] = B64[(t >> 18) & 0x3F];
		out[o++] = B64[(t >> 12) & 0x3F];
		out[o++] = (i + 1 < len) ? B64[(t >> 6) & 0x3F] : '=';
		out[o++] = '=';
	}
	out[o] = '\0';
	return out;
}

void gsxcu_warn_pcre_only(const char *expr) {
	static const char *const PCRE[] = {"(?<", "(?=", "(?!", "\\d", "\\w",
									   "\\s", "\\b", "+?", "*?"};
	if (!expr) return;
	for (size_t i = 0; i < sizeof PCRE / sizeof PCRE[0]; i++) {
		if (!strstr(expr, PCRE[i])) continue;
		log_warn("sxcu: `%s` is PCRE-only; grabit matches with POSIX ERE, so "
				 "this will not behave as it does in ShareX: %s",
				 PCRE[i], expr);
		return;
	}
}

char *gsxcu_random_pipe_part(const char *arg) {
	size_t n = 1;
	for (const char *p = arg; *p; p++) {
		if (*p == '|') n++;
	}
	unsigned char r = 0;
	if (n < 2 || grabit_read_random(&r, 1) != 0) return gsxcu_first_pipe_part(arg);

	size_t pick = r % n;
	const char *start = arg;
	for (size_t i = 0; i < pick; i++) {
		start = strchr(start, '|') + 1;
	}
	const char *end = strchr(start, '|');
	return end ? strndup(start, (size_t)(end - start)) : strdup(start);
}

char *gsxcu_first_pipe_part(const char *arg) {
	const char *bar = strchr(arg, '|');
	return bar ? strndup(arg, (size_t)(bar - arg)) : strdup(arg);
}

bool gsxcu_all_digits(const char *s) {
	if (!*s) return false;
	for (; *s; s++) {
		if (*s < '0' || *s > '9') return false;
	}
	return true;
}

const char *gsxcu_find_close(const char *s, char close, char open) {
	if (open == close) {
		for (const char *p = s; *p; p++) {
			if (*p == '\\' && p[1]) {
				p++;
				continue;
			}
			if (*p == close) return p;
		}
		return NULL;
	}
	int depth = 1;
	for (const char *p = s; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			continue;
		}
		if (*p == open)
			depth++;
		else if (*p == close) {
			if (--depth == 0) return p;
		}
	}
	return NULL;
}
