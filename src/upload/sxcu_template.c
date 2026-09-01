// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/sxcu.h"

#include "log.h"
#include "util/json_path.h"
#include "util/util.h"

#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <json-c/json.h>

#define SXCU_MAX_TEMPLATE_DEPTH 8
#define SXCU_REGEX_INLINE_GROUPS 8

struct ctx {
	const char *file_path;
	const char *body;
	const struct sxcu_kv *resp_headers;
	size_t n_resp_headers;
	const char *response_url;
	char *const *regex_list;
	size_t n_regex_list;
	struct json_object *json_root;
	bool json_parsed;
	bool unresolved;
};

static int expand_into(const char *tmpl, struct ctx *c, struct grabit_buf *out, int depth);

static char *match_regex_text(const char *body, const char *pattern, int group_idx) {
	regex_t re;
	if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
		log_warn("sxcu: invalid regex: %s", pattern);
		return NULL;
	}
	char *bounded = NULL;
	enum { REGEX_SUBJECT_MAX = 256u << 10 };
	if (strlen(body) > REGEX_SUBJECT_MAX) {
		bounded = strndup(body, REGEX_SUBJECT_MAX);
		if (bounded) body = bounded;
	}
	size_t ngroups = re.re_nsub + 1;
	regmatch_t small[SXCU_REGEX_INLINE_GROUPS];
	regmatch_t *m = (ngroups <= SXCU_REGEX_INLINE_GROUPS) ? small : calloc(ngroups, sizeof *m);
	if (!m) {
		regfree(&re);
		free(bounded);
		return NULL;
	}
	char *out = NULL;
	if (regexec(&re, body, ngroups, m, 0) == 0) {
		size_t g = (group_idx >= 0 && (size_t)group_idx < ngroups) ? (size_t)group_idx : 0;
		if (m[g].rm_so >= 0) {
			out = strndup(body + m[g].rm_so, (size_t)(m[g].rm_eo - m[g].rm_so));
		}
	}
	if (m != small) free(m);
	regfree(&re);
	free(bounded);
	return out;
}

static char *do_regex(const char *arg, struct ctx *c) {
	if (!arg || !c->body) return NULL;
	const char *bar = strrchr(arg, '|');
	if (bar && !gsxcu_all_digits(bar + 1)) bar = NULL;
	if (!bar) {
		const char *comma = strrchr(arg, ',');
		if (comma && gsxcu_all_digits(comma + 1)) bar = comma;
	}
	int group = 0;
	const char *pattern = arg;
	char *pattern_alloc = NULL;
	if (bar) {
		pattern_alloc = strndup(arg, (size_t)(bar - arg));
		pattern = pattern_alloc;
		group = atoi(bar + 1);
	}
	char *idx_end = NULL;
	long n = strtol(pattern, &idx_end, 10);
	const char *eff_pattern = pattern;
	if (idx_end != pattern && *idx_end == '\0' && c->regex_list && n >= 1 &&
		(size_t)n <= c->n_regex_list) {
		eff_pattern = c->regex_list[n - 1];
	}
	char *result = match_regex_text(c->body, eff_pattern, group);
	free(pattern_alloc);
	return result;
}

static char *do_json(const char *arg, struct ctx *c) {
	if (!arg || !c->body) return NULL;
	if (!c->json_parsed) {
		c->json_parsed = true;
		c->json_root = json_tokener_parse(c->body);
	}
	if (!c->json_root) return NULL;
	const char *path = arg;
	if (path[0] == '$' && path[1] == '.') path += 2;
	return grabit_json_path_string(c->json_root, path);
}

static char *do_header(const char *arg, struct ctx *c) {
	if (!arg) return NULL;
	for (size_t i = 0; i < c->n_resp_headers; i++) {
		if (strcasecmp(c->resp_headers[i].k, arg) == 0) {
			return strdup(c->resp_headers[i].v);
		}
	}
	return NULL;
}

static char *expand_token(const char *name, const char *arg, struct ctx *c) {
	if (strcmp(name, "filename") == 0)
		return strdup(c->file_path ? grabit_basename(c->file_path) : "");
	if (strcmp(name, "input") == 0) return strdup("");
	if (strcmp(name, "base64") == 0) return gsxcu_base64_encode(arg ? arg : "");
	if (strcmp(name, "random") == 0 && arg) return gsxcu_random_pipe_part(arg);
	if (strcmp(name, "select") == 0 && arg) return gsxcu_first_pipe_part(arg);
	if (strcmp(name, "prompt") == 0 || strcmp(name, "inputbox") == 0) {
		const char *bar = arg ? strchr(arg, '|') : NULL;
		return strdup(bar ? bar + 1 : "");
	}
	if (strcmp(name, "response") == 0 && c->body)
		return strdup(c->body);
	if (strcmp(name, "responseurl") == 0)
		return strdup(c->response_url ? c->response_url : "");
	if (strcmp(name, "header") == 0) return do_header(arg, c);
	if (strcmp(name, "json") == 0) return do_json(arg, c);
	if (strcmp(name, "regex") == 0) return do_regex(arg, c);
	return NULL;
}

static int expand_into(const char *tmpl, struct ctx *c, struct grabit_buf *out, int depth) {
	if (depth > SXCU_MAX_TEMPLATE_DEPTH || !tmpl) return -1;
	const char *p = tmpl;
	while (*p) {
		if (*p == '\\' && p[1]) {
			if (grabit_buf_putc(out, p[1]) != 0) return -1;
			p += 2;
			continue;
		}
		char close = 0;
		if (*p == '{')
			close = '}';
		else if (*p == '$')
			close = '$';
		const char *end = close ? gsxcu_find_close(p + 1, close, *p) : NULL;
		if (!end) {
			if (grabit_buf_putc(out, *p) != 0) return -1;
			p++;
			continue;
		}
		size_t tok_len = (size_t)(end - p - 1);

		bool flat = true;
		for (size_t i = 0; i < tok_len; i++) {
			char ch = p[1 + i];
			if (ch == '\\' || ch == '{' || ch == '$') {
				flat = false;
				break;
			}
		}

		char *raw = strndup(p + 1, tok_len);
		if (!raw) return -1;

		struct grabit_buf inner = {0};
		char *body_str = raw;
		int rc = 0;
		if (!flat) {
			rc = expand_into(raw, c, &inner, depth + 1);
			if (rc == 0) rc = grabit_buf_putc(&inner, '\0');
			body_str = inner.data;
		}
		if (rc != 0) {
			free(raw);
			grabit_buf_free(&inner);
			return -1;
		}

		char *colon = strchr(body_str, ':');
		char *arg = NULL;
		if (colon) {
			*colon = '\0';
			arg = colon + 1;
		}
		char *replacement = expand_token(body_str, arg, c);
		int put_rc = 0;
		if (replacement) {
			put_rc = grabit_buf_puts(out, replacement);
			free(replacement);
		} else {
			c->unresolved = true;
			put_rc = grabit_buf_putc(out, *p) != 0 ||
					 grabit_buf_putn(out, p + 1, tok_len) != 0 ||
					 grabit_buf_putc(out, close) != 0;
		}
		free(raw);
		grabit_buf_free(&inner);
		if (put_rc != 0) return -1;
		p = end + 1;
	}
	return 0;
}

static char *do_expand(const char *tmpl, struct ctx *c) {
	if (!tmpl) return strdup("");
	struct grabit_buf b = {0};
	if (expand_into(tmpl, c, &b, 0) != 0 || grabit_buf_putc(&b, '\0') != 0) {
		grabit_buf_free(&b);
		return NULL;
	}
	return b.data;
}

char *sxcu_expand_input(const char *tmpl, const char *file_path) {
	struct ctx c = {.file_path = file_path};
	return do_expand(tmpl, &c);
}

char *sxcu_expand_response(const char *tmpl, const char *body,
						   const struct sxcu_kv *resp_headers, size_t n_resp_headers,
						   const char *response_url,
						   char *const *regex_list, size_t n_regex_list) {
	struct ctx c = {
		.body = body,
		.resp_headers = resp_headers,
		.n_resp_headers = n_resp_headers,
		.response_url = response_url,
		.regex_list = regex_list,
		.n_regex_list = n_regex_list,
	};
	char *out = do_expand(tmpl, &c);
	if (c.json_root) json_object_put(c.json_root);
	if (c.unresolved) {
		free(out);
		return NULL;
	}
	return out;
}
