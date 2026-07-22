// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "log.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *VALS_zl_format[] = {"random", "date", "uuid", "name", "gfycat", NULL};
static const char *VALS_zl_compress[] = {"jpg", "png", "webp", "jxl", NULL};
static const char *VALS_zl_true_only[] = {"true", NULL};

const struct zl_hdr gcfg_zl_headers[] = {
	{"x-zipline-deletes-at", ZL_FREE, NULL},
	{"x-zipline-format", ZL_ENUM, VALS_zl_format},
	{"x-zipline-image-compression-percent", ZL_INT_PCT, NULL},
	{"x-zipline-image-compression-type", ZL_ENUM, VALS_zl_compress},
	{"x-zipline-password", ZL_FREE, NULL},
	{"x-zipline-max-views", ZL_INT, NULL},
	{"x-zipline-no-json", ZL_ENUM, VALS_zl_true_only},
	{"x-zipline-original-name", ZL_ENUM, VALS_zl_true_only},
	{"x-zipline-folder", ZL_FREE, NULL},
	{"x-zipline-filename", ZL_FREE, NULL},
	{"x-zipline-domain", ZL_FREE, NULL},
	{"x-zipline-file-extension", ZL_FREE, NULL},
};
const size_t gcfg_zl_headers_n = sizeof gcfg_zl_headers / sizeof gcfg_zl_headers[0];

const struct zl_hdr *gcfg_zl_find(const char *name) {
	for (size_t i = 0; i < gcfg_zl_headers_n; i++) {
		if (strcmp(gcfg_zl_headers[i].name, name) == 0) return &gcfg_zl_headers[i];
	}
	return NULL;
}

int gcfg_validate_zl_header(const char *hdr, const char *value) {
	const struct zl_hdr *spec = gcfg_zl_find(hdr);
	if (!spec) {
		log_warn("unknown zipline header %s; forwarding as-is", hdr);
		return 0;
	}
	switch (spec->kind) {
	case ZL_FREE:
		return 0;
	case ZL_ENUM:
		if (cfg_in_list(value, spec->allowed)) return 0;
		if (spec->allowed[0] && !spec->allowed[1]) {
			log_error("%s must be \"%s\" (omit the header to disable)",
					  hdr, spec->allowed[0]);
		} else {
			struct grabit_buf b = {0};
			for (size_t i = 0; spec->allowed[i]; i++) {
				if (i) grabit_buf_putc(&b, '|');
				grabit_buf_puts(&b, spec->allowed[i]);
			}
			log_error("%s must be one of %s", hdr, b.data ? b.data : "(none)");
			grabit_buf_free(&b);
		}
		return -1;
	case ZL_INT:
	case ZL_INT_PCT: {
		if (!*value) {
			log_error("%s must be an integer", hdr);
			return -1;
		}
		char *end = NULL;
		long n = strtol(value, &end, 10);
		if (!end || *end != '\0') {
			log_error("%s must be an integer", hdr);
			return -1;
		}
		if (spec->kind == ZL_INT_PCT && (n < 0 || n > 100)) {
			log_error("%s must be between 0 and 100", hdr);
			return -1;
		}
		if (spec->kind == ZL_INT && n < 0) {
			log_error("%s must be a non-negative integer", hdr);
			return -1;
		}
		return 0;
	}
	}
	return -1;
}

char *gcfg_normalize_zipline_domain(const char *value) {
	if (!value || !*value) return NULL;
	bool has_scheme = strncmp(value, "http://", 7) == 0 ||
					  strncmp(value, "https://", 8) == 0;
	size_t vlen = strlen(value);
	while (vlen > 0 && value[vlen - 1] == '/')
		vlen--;
	const char *suffix = "/api/upload";
	size_t slen = strlen(suffix);
	bool has_path = vlen >= slen && strncmp(value + vlen - slen, suffix, slen) == 0;
	char *out = NULL;
	int rc;
	if (has_scheme && has_path)
		rc = grabit_xasprintf(&out, "%.*s", (int)vlen, value);
	else if (has_scheme)
		rc = grabit_xasprintf(&out, "%.*s/api/upload", (int)vlen, value);
	else if (has_path)
		rc = grabit_xasprintf(&out, "https://%.*s", (int)vlen, value);
	else
		rc = grabit_xasprintf(&out, "https://%.*s/api/upload", (int)vlen, value);
	return rc == 0 ? out : NULL;
}
