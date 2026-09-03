// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include <ctype.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 512

struct strset {
	char *items[MAX_ITEMS];
	size_t n;
};

static int g_missing;
static const char *g_bin;
static const char *g_name;
static char g_options[1 << 20];
static char g_readme[1 << 16];

static void note(const char *fmt, const char *a, const char *b) {
	fprintf(stderr, "  ");
	fprintf(stderr, fmt, a, b);
	fprintf(stderr, "\n");
	g_missing++;
}

static bool set_has(const struct strset *s, const char *v) {
	for (size_t i = 0; i < s->n; i++)
		if (strcmp(s->items[i], v) == 0) return true;
	return false;
}

static void set_add(struct strset *s, const char *v) {
	if (s->n == MAX_ITEMS || set_has(s, v)) return;
	s->items[s->n] = strdup(v);
	if (s->items[s->n]) s->n++;
}

static void set_free(struct strset *s) {
	for (size_t i = 0; i < s->n; i++)
		free(s->items[i]);
	s->n = 0;
}

static bool read_file(const char *path, char *buf, size_t cap) {
	FILE *f = fopen(path, "r");
	if (!f) return false;
	size_t n = fread(buf, 1, cap - 1, f);
	buf[n] = '\0';
	fclose(f);
	return true;
}

static bool token_at(const char *hay, const char *tok) {
	size_t n = strlen(tok);
	for (const char *p = strstr(hay, tok); p; p = strstr(p + 1, tok)) {
		char before = p == hay ? '\0' : p[-1];
		char after = p[n];
		bool ok_before = !(isalnum((unsigned char)before) || before == '_' ||
						   before == '.' || before == '-');
		bool ok_after = !(isalnum((unsigned char)after) || after == '_' ||
						  after == '.' || after == '-');
		if (ok_before && ok_after) return true;
	}
	return false;
}

static void collect_cli(struct strset *out, const char *args, int field) {
	char cmd[512];
	snprintf(cmd, sizeof cmd, "%s %s 2>/dev/null", g_bin, args);
	FILE *p = popen(cmd, "r");
	if (!p) return;
	char line[512];
	while (fgets(line, sizeof line, p)) {
		if (line[0] != ' ' || line[1] != ' ') continue;
		char *k = line + 2;
		char *e = k;
		while (*e && (islower((unsigned char)*e) || *e == '_' || *e == '.'))
			e++;
		char end = *e;
		*e = '\0';
		if (e == k) continue;
		if (field && end != ' ') continue;
		if (!field && end != '\n') continue;
		if (strstr(k, ".tool.")) continue;
		set_add(out, k);
	}
	pclose(p);
}

static bool key_exists(const char *key) {
	char cmd[512];
	snprintf(cmd, sizeof cmd, "%s set %s >/dev/null 2>&1", g_bin, key);
	return system(cmd) == 0;
}

static void check_keys(void) {
	struct strset keys = {0};
	collect_cli(&keys, "set", 0);
	collect_cli(&keys, "set keys", 1);
	if (keys.n == 0) {
		note("could not enumerate config keys from %s%s", g_bin, "");
		return;
	}
	for (size_t i = 0; i < keys.n; i++)
		if (!token_at(g_options, keys.items[i]))
			note("undocumented key: %s%s", keys.items[i], "");

	for (const char *p = g_options; (p = strstr(p, "\n| `")); p += 4) {
		const char *k = p + 4;
		const char *e = k;
		while (*e && (islower((unsigned char)*e) || *e == '_' || *e == '.'))
			e++;
		if (*e != '`' || e == k) continue;
		char buf[128];
		size_t n = (size_t)(e - k);
		if (n >= sizeof buf) continue;
		memcpy(buf, k, n);
		buf[n] = '\0';
		if (set_has(&keys, buf) || key_exists(buf)) continue;
		note("documented key does not exist: %s%s", buf, "");
	}
	set_free(&keys);
}

static struct strset g_proto;
static struct strset g_proto_found;

static int proto_visit(const char *path, const struct stat *sb, int type,
					   struct FTW *ftw) {
	(void)sb;
	(void)ftw;
	if (type != FTW_F) return 0;
	static char buf[1 << 21];
	if (!read_file(path, buf, sizeof buf)) return 0;
	for (size_t i = 0; i < g_proto.n; i++)
		if (strstr(buf, g_proto.items[i])) set_add(&g_proto_found, g_proto.items[i]);
	return 0;
}

static void scan_protocols(const char *text) {
	for (const char *p = text; *p;) {
		if (!islower((unsigned char)*p) ||
			(p != text && (isalnum((unsigned char)p[-1]) || p[-1] == '_' ||
						   p[-1] == '-' || p[-1] == '.'))) {
			p++;
			continue;
		}
		const char *e = p;
		while (*e && (islower((unsigned char)*e) || isdigit((unsigned char)*e) ||
					  *e == '_' || *e == '-'))
			e++;
		size_t n = (size_t)(e - p);
		if (n > 4 && n < 96) {
			const char *tail = e - 1;
			while (tail > p && isdigit((unsigned char)*tail))
				tail--;
			if (*tail == 'v' && (tail[-1] == '_' || tail[-1] == '-')) {
				char buf[96];
				memcpy(buf, p, n);
				buf[n] = '\0';
				set_add(&g_proto, buf);
			}
		}
		p = e;
	}
}

static void check_protocols(const char *const *docs, size_t n_docs) {
	static char buf[1 << 20];
	for (size_t i = 0; i < n_docs; i++)
		if (read_file(docs[i], buf, sizeof buf)) scan_protocols(buf);
	const char *roots[] = {"protocols", "src", "include"};
	for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++)
		nftw(roots[i], proto_visit, 16, FTW_PHYS);
	for (size_t i = 0; i < g_proto.n; i++)
		if (!set_has(&g_proto_found, g_proto.items[i]))
			note("docs name an unknown protocol: %s%s", g_proto.items[i], "");
	set_free(&g_proto);
	set_free(&g_proto_found);
}

static void check_man_version(const char *version) {
	char path[256], want[256];
	static char buf[1 << 20];
	snprintf(path, sizeof path, "man/%s.1", g_name);
	snprintf(want, sizeof want, "\"%s %s\"", g_name, version);
	if (!read_file(path, buf, sizeof buf) || !strstr(buf, ".TH GRABIT 1 ") ||
		!strstr(buf, want))
		note("%s .TH is not %s", path, want);
}

static bool readme_names(const char *base) {
	for (const char *p = g_readme; (p = strchr(p, '`')); p++) {
		const char *e = strchr(p + 1, '`');
		if (!e) break;
		size_t n = (size_t)(e - p - 1);
		char tok[128];
		if (n && n < sizeof tok) {
			memcpy(tok, p + 1, n);
			tok[n] = '\0';
			const char *t = tok;
			if (strncmp(t, "lib", 3) == 0 && strcmp(t, base) != 0) t += 3;
			size_t bl = strlen(base);
			if (strncmp(t, base, bl) == 0 &&
				(t[bl] == '\0' || (t[bl] == '-' && isdigit((unsigned char)t[bl + 1]))))
				return true;
		}
		p = e;
	}
	return false;
}

static void check_deps(int argc, char **argv, int first) {
	for (int i = first; i < argc; i++) {
		char base[128];
		snprintf(base, sizeof base, "%s", argv[i]);
		for (char *d = base; (d = strchr(d, '-')); d++) {
			if (isdigit((unsigned char)d[1])) {
				*d = '\0';
				break;
			}
		}
		if (!readme_names(base)) note("README build deps missing: %s%s", argv[i], "");
	}
}

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: %s <binary> <name> <version> [pkg ...]\n", argv[0]);
		return 2;
	}
	g_bin = argv[1];
	g_name = argv[2];
	const char *version = argv[3];

	char mandoc[256];
	snprintf(mandoc, sizeof mandoc, "man/%s.1", g_name);
	const char *docs[] = {"README.md", "OPTIONS.md", "PLUGINS.md", mandoc};

	if (!read_file("OPTIONS.md", g_options, sizeof g_options) ||
		!read_file("README.md", g_readme, sizeof g_readme)) {
		fprintf(stderr, "check_docs: run from the repository root\n");
		return 2;
	}

	check_keys();
	check_protocols(docs, sizeof docs / sizeof docs[0]);
	check_man_version(version);
	check_deps(argc, argv, 4);

	if (g_missing) {
		fprintf(stderr, "docs out of sync\n");
		return 1;
	}
	puts("docs in sync");
	return 0;
}
