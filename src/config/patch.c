// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "log.h"
#include "paths.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_MAX_FILE (1u << 20)

struct line {
	const char *p;
	size_t len;
};

static void trim(const char **p, size_t *len) {
	while (*len > 0 && (**p == ' ' || **p == '\t')) {
		(*p)++;
		(*len)--;
	}
	while (*len > 0) {
		char c = (*p)[*len - 1];
		if (c != ' ' && c != '\t' && c != '\r') break;
		(*len)--;
	}
}

static void unquote(const char **p, size_t *len) {
	if (*len >= 2 && ((**p == '"' && (*p)[*len - 1] == '"') ||
					  (**p == '\'' && (*p)[*len - 1] == '\''))) {
		(*p)++;
		*len -= 2;
	}
}

static bool line_is_section(const struct line *l, char *out, size_t cap) {
	const char *p = l->p;
	size_t len = l->len;
	trim(&p, &len);
	if (len < 2 || p[0] != '[' || p[len - 1] != ']') return false;
	p++;
	len -= 2;
	trim(&p, &len);

	size_t off = 0;
	while (len > 0) {
		const char *seg = p;
		size_t seglen = 0;
		bool quoted = (*p == '"' || *p == '\'');
		if (quoted) {
			char q = *p;
			seglen = 1;
			while (seglen < len && p[seglen] != q)
				seglen++;
			if (seglen < len) seglen++;
		} else {
			while (seglen < len && p[seglen] != '.')
				seglen++;
		}
		const char *s = seg;
		size_t sl = seglen;
		trim(&s, &sl);
		unquote(&s, &sl);
		if (off + sl + 2 >= cap) return false;
		if (off > 0) out[off++] = '.';
		memcpy(out + off, s, sl);
		off += sl;

		p += seglen;
		len -= seglen;
		trim(&p, &len);
		if (len > 0 && *p == '.') {
			p++;
			len--;
			trim(&p, &len);
		}
	}
	out[off] = '\0';
	return true;
}

static bool line_key(const struct line *l, const char *section, char *out, size_t cap) {
	const char *p = l->p;
	size_t len = l->len;
	trim(&p, &len);
	if (len == 0 || *p == '#' || *p == '[') return false;

	const char *eq = memchr(p, '=', len);
	if (!eq) return false;
	const char *k = p;
	size_t klen = (size_t)(eq - p);
	trim(&k, &klen);
	unquote(&k, &klen);
	if (klen == 0) return false;

	int n = section[0] ? snprintf(out, cap, "%s.%.*s", section, (int)klen, k)
					   : snprintf(out, cap, "%.*s", (int)klen, k);
	return n > 0 && (size_t)n < cap;
}

static size_t value_end(const char *p, size_t len) {
	size_t i = 0;
	while (i < len && (p[i] == ' ' || p[i] == '\t'))
		i++;
	if (i >= len) return i;
	if (p[i] == '"' || p[i] == '\'') {
		char q = p[i++];
		while (i < len) {
			if (p[i] == '\\' && q == '"' && i + 1 < len) {
				i += 2;
				continue;
			}
			if (p[i] == q) return i + 1;
			i++;
		}
		return i;
	}
	if (p[i] == '[') {
		int depth = 0;
		while (i < len) {
			if (p[i] == '[')
				depth++;
			else if (p[i] == ']' && --depth == 0)
				return i + 1;
			i++;
		}
		return i;
	}
	size_t end = i;
	while (i < len && p[i] != '#') {
		if (p[i] != ' ' && p[i] != '\t') end = i + 1;
		i++;
	}
	return end;
}

static bool is_bare_safe(const char *v) {
	if (strcmp(v, "true") == 0 || strcmp(v, "false") == 0) return true;
	const char *p = v;
	if (*p == '-') p++;
	if (*p == '0' && p[1] && p[1] != '.') return false;
	bool digits = false;
	while (*p >= '0' && *p <= '9') {
		p++;
		digits = true;
	}
	if (!digits) return false;
	if (*p == '.') {
		p++;
		if (!(*p >= '0' && *p <= '9')) return false;
		while (*p >= '0' && *p <= '9')
			p++;
	}
	return *p == '\0';
}

static bool emit_kv(struct grabit_buf *out, const char *lhs, size_t lhs_len,
					const char *key, const char *value, bool prefer_bare,
					const char *comment, size_t comment_len) {
	if (lhs_len > 0) {
		if (grabit_buf_putn(out, lhs, lhs_len) != 0) return false;
	} else {
		const char *dot = strrchr(key, '.');
		gcfg_emit_key(out, dot ? dot + 1 : key);
		if (grabit_buf_putc(out, ' ') != 0) return false;
	}
	if (grabit_buf_puts(out, "= ") != 0) return false;
	if (prefer_bare && is_bare_safe(value)) {
		if (grabit_buf_puts(out, value) != 0) return false;
	} else {
		gcfg_emit_value(out, key, value);
	}
	if (comment_len > 0) {
		if (grabit_buf_putc(out, ' ') != 0) return false;
		if (grabit_buf_putn(out, comment, comment_len) != 0) return false;
	}
	return grabit_buf_putc(out, '\n') == 0;
}

static bool key_matches(const char *full, const char *target, bool prefix) {
	return prefix ? strncmp(full, target, strlen(target)) == 0
				  : strcmp(full, target) == 0;
}

static int split_lines(char *text, size_t len, struct line **out, size_t *n_out) {
	size_t n = 0;
	for (size_t i = 0; i < len; i++)
		if (text[i] == '\n') n++;
	if (len > 0 && text[len - 1] != '\n') n++;

	struct line *lines = calloc(n ? n : 1, sizeof *lines);
	if (!lines) return -1;

	size_t idx = 0, start = 0;
	for (size_t i = 0; i < len; i++) {
		if (text[i] != '\n') continue;
		lines[idx].p = text + start;
		lines[idx].len = i - start;
		idx++;
		start = i + 1;
	}
	if (start < len) {
		lines[idx].p = text + start;
		lines[idx].len = len - start;
		idx++;
	}
	*out = lines;
	*n_out = idx;
	return 0;
}

int cfg_file_edit(const char *path, const char *key, const char *value, bool prefix) {
	char *text = NULL;
	size_t len = 0;
	if (grabit_read_file(path, CFG_MAX_FILE, &text, &len) != 0) return -1;

	struct line *lines = NULL;
	size_t n_lines = 0;
	if (split_lines(text, len, &lines, &n_lines) != 0) {
		free(text);
		return -1;
	}

	const char *dot = strrchr(key, '.');
	char want_section[256] = {0};
	if (dot && !prefix) {
		size_t sl = (size_t)(dot - key);
		if (sl >= sizeof want_section) {
			free(lines);
			free(text);
			return -1;
		}
		memcpy(want_section, key, sl);
	}

	bool found = false;
	size_t insert_line = 0;
	bool have_insert = (want_section[0] == '\0');
	{
		char section[256] = {0};
		bool in_want = have_insert;
		for (size_t i = 0; i < n_lines; i++) {
			char sec[256];
			bool is_header = line_is_section(&lines[i], sec, sizeof sec);
			if (is_header) {
				snprintf(section, sizeof section, "%s", sec);
				in_want = strcmp(section, want_section) == 0;
			}
			char full[512];
			bool is_kv = line_key(&lines[i], section, full, sizeof full);
			if (is_kv && key_matches(full, key, prefix)) found = true;
			if (in_want && (is_header || is_kv)) {
				insert_line = i + 1;
				have_insert = true;
			}
		}
	}

	bool need_new = value && !found;
	if (need_new && !have_insert) insert_line = n_lines;

	struct grabit_buf out = {0};
	char section[256] = {0};

	for (size_t i = 0; i <= n_lines; i++) {
		if (need_new && i == insert_line) {
			if (!have_insert) {
				if (out.len > 0 && out.data[out.len - 1] != '\n')
					grabit_buf_putc(&out, '\n');
				grabit_buf_putc(&out, '\n');
				if (gcfg_emit_section(&out, want_section, strlen(want_section)) != 0)
					goto oom;
			}
			if (!emit_kv(&out, NULL, 0, key, value, false, NULL, 0)) goto oom;
		}
		if (i == n_lines) break;

		char sec[256];
		if (line_is_section(&lines[i], sec, sizeof sec))
			snprintf(section, sizeof section, "%s", sec);

		char full[512];
		if (line_key(&lines[i], section, full, sizeof full) &&
			key_matches(full, key, prefix)) {
			if (!value) continue;
			const char *p = lines[i].p;
			size_t plen = lines[i].len;
			const char *eq = memchr(p, '=', plen);
			size_t lhs_len = (size_t)(eq - p);
			const char *rhs = eq + 1;
			size_t rhs_len = plen - lhs_len - 1;
			size_t vend = value_end(rhs, rhs_len);
			const char *comment = rhs + vend;
			size_t comment_len = rhs_len - vend;
			trim(&comment, &comment_len);
			if (comment_len > 0 && comment[0] != '#') comment_len = 0;
			const char *vs = rhs;
			size_t vlen = rhs_len;
			trim(&vs, &vlen);
			bool was_bare = vlen > 0 && *vs != '"' && *vs != '\'';
			if (!emit_kv(&out, p, lhs_len, key, value, was_bare, comment, comment_len))
				goto oom;
			continue;
		}

		if (grabit_buf_putn(&out, lines[i].p, lines[i].len) != 0) goto oom;
		if (grabit_buf_putc(&out, '\n') != 0) goto oom;
	}

	int rc = paths_atomic_write(path, out.data ? out.data : "\n", out.len ? out.len : 1);
	grabit_buf_free(&out);
	free(lines);
	free(text);
	return rc;

oom:
	grabit_buf_free(&out);
	free(lines);
	free(text);
	return -1;
}
