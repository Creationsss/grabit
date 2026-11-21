// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/icons.h"

#include "util/util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vendor/nanosvg/nanosvg.h"
#include "vendor/nanosvg/nanosvgrast.h"

#define MAX_THEMES 12
#define MAX_BASES 12
#define PATH_CAP 4096
#define OTHER_THEME_RANK 100
#define PIXMAP_RANK 200

static const char *const CONTEXTS[] = {"apps", "devices", "actions", "status",
									   "places", "categories", "legacy", NULL};

struct ui_icon_scan {
	char themes[MAX_THEMES][128];
	size_t n_themes;
	char bases[MAX_BASES][PATH_CAP];
	size_t n_bases;
	char pixmaps[MAX_BASES][PATH_CAP];
	size_t n_pixmaps;
};

static uint64_t hash_name(const char *s, size_t n) {
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++)
		h = (h ^ (unsigned char)s[i]) * 1099511628211ull;
	return h;
}

static size_t *find_slot(const struct ui_icons *ix, const char *name, size_t len) {
	if (!ix->n_slots) return NULL;
	size_t mask = ix->n_slots - 1;
	for (size_t i = hash_name(name, len) & mask;; i = (i + 1) & mask) {
		size_t *slot = &ix->slots[i];
		if (!*slot) return slot;
		const char *have = ix->v[*slot - 1].name;
		if (strlen(have) == len && memcmp(have, name, len) == 0) return slot;
	}
}

static int grow_slots(struct ui_icons *ix) {
	size_t n = ix->n_slots ? ix->n_slots * 2 : 4096;
	size_t *slots = calloc(n, sizeof *slots);
	if (!slots) return -1;
	free(ix->slots);
	ix->slots = slots;
	ix->n_slots = n;
	for (size_t i = 0; i < ix->n; i++)
		*find_slot(ix, ix->v[i].name, strlen(ix->v[i].name)) = i + 1;
	return 0;
}

static void add_icon(struct ui_icons *ix, const char *name, size_t len, const char *path,
					 int rank) {
	if ((ix->n + 1) * 2 > ix->n_slots && grow_slots(ix) != 0) return;
	size_t *slot = find_slot(ix, name, len);
	if (*slot) {
		struct ui_icon_entry *e = &ix->v[*slot - 1];
		if (rank >= e->rank) return;
		char *p = strdup(path);
		if (!p) return;
		free(e->path);
		e->path = p;
		e->rank = rank;
		return;
	}
	if (ix->n == ix->cap) {
		size_t cap = ix->cap ? ix->cap * 2 : 1024;
		struct ui_icon_entry *v = realloc(ix->v, cap * sizeof *v);
		if (!v) return;
		ix->v = v;
		ix->cap = cap;
	}
	char *n = strndup(name, len), *p = strdup(path);
	if (!n || !p) {
		free(n);
		free(p);
		return;
	}
	ix->v[ix->n] = (struct ui_icon_entry){n, p, rank};
	*slot = ++ix->n;
}

static void trim(char *s) {
	size_t n = grabit_rstrip(s, strlen(s));
	size_t lead = strspn(s, " \t\"'");
	if (lead) memmove(s, s + lead, strlen(s + lead) + 1);
	n = strlen(s);
	while (n && (s[n - 1] == '"' || s[n - 1] == '\''))
		s[--n] = '\0';
}

static bool ini_value(const char *path, const char *section, const char *key, char *out,
					  size_t cap) {
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char line[1024];
	bool in = false, found = false;
	size_t klen = strlen(key);
	while (!found && fgets(line, sizeof line, f)) {
		trim(line);
		if (line[0] == '[') {
			in = strncmp(line + 1, section, strlen(section)) == 0 &&
				 line[1 + strlen(section)] == ']';
			continue;
		}
		if (!in || strncmp(line, key, klen) != 0) continue;
		const char *p = line + klen;
		while (*p == ' ')
			p++;
		if (*p != '=') continue;
		snprintf(out, cap, "%s", p + 1);
		trim(out);
		found = out[0] != '\0';
	}
	fclose(f);
	return found;
}

static void push_theme(struct ui_icon_scan *s, const char *name, size_t len) {
	if (!len || len >= sizeof s->themes[0] || s->n_themes >= MAX_THEMES) return;
	for (size_t i = 0; i < s->n_themes; i++)
		if (strlen(s->themes[i]) == len && strncmp(s->themes[i], name, len) == 0) return;
	memcpy(s->themes[s->n_themes], name, len);
	s->themes[s->n_themes++][len] = '\0';
}

static void push_dir(char (*list)[PATH_CAP], size_t *n, const char *a, size_t alen,
					 const char *suffix) {
	if (*n >= MAX_BASES) return;
	int w = snprintf(list[*n], PATH_CAP, "%.*s%s", (int)alen, a, suffix);
	if (w > 0 && w < PATH_CAP) (*n)++;
}

static const char *abs_env(const char *name) {
	const char *v = getenv(name);
	return v && v[0] == '/' ? v : NULL;
}

static void find_bases(struct ui_icon_scan *s) {
	const char *home = abs_env("HOME");
	const char *dh = abs_env("XDG_DATA_HOME");
	if (dh)
		push_dir(s->bases, &s->n_bases, dh, strlen(dh), "/icons");
	else if (home)
		push_dir(s->bases, &s->n_bases, home, strlen(home), "/.local/share/icons");
	if (home) push_dir(s->bases, &s->n_bases, home, strlen(home), "/.icons");
	const char *dirs = getenv("XDG_DATA_DIRS");
	if (!dirs || !dirs[0]) dirs = "/usr/local/share:/usr/share";
	for (const char *p = dirs; *p;) {
		const char *e = strchr(p, ':');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		if (n && p[0] == '/') {
			push_dir(s->bases, &s->n_bases, p, n, "/icons");
			push_dir(s->pixmaps, &s->n_pixmaps, p, n, "/pixmaps");
		}
		if (!e) break;
		p = e + 1;
	}
}

static void user_theme(struct ui_icon_scan *s) {
	char path[PATH_CAP * 2], val[256];
	const char *home = abs_env("HOME");
	const char *ch = abs_env("XDG_CONFIG_HOME");
	char cfg[PATH_CAP];
	if (ch)
		snprintf(cfg, sizeof cfg, "%s", ch);
	else if (home)
		snprintf(cfg, sizeof cfg, "%s/.config", home);
	else
		return;
	static const char *const FILES[][3] = {
		{"gtk-3.0/settings.ini", "Settings", "gtk-icon-theme-name"},
		{"gtk-4.0/settings.ini", "Settings", "gtk-icon-theme-name"},
		{"kdeglobals", "Icons", "Theme"},
	};
	for (size_t i = 0; i < sizeof FILES / sizeof *FILES; i++) {
		if (snprintf(path, sizeof path, "%s/%s", cfg, FILES[i][0]) >= (int)sizeof path) continue;
		if (ini_value(path, FILES[i][1], FILES[i][2], val, sizeof val)) {
			push_theme(s, val, strlen(val));
			return;
		}
	}
}

static void add_inherits(struct ui_icon_scan *s) {
	for (size_t t = 0; t < s->n_themes; t++) {
		char val[512];
		for (size_t b = 0; b < s->n_bases; b++) {
			char path[PATH_CAP];
			if (snprintf(path, sizeof path, "%s/%s/index.theme", s->bases[b], s->themes[t]) >=
				(int)sizeof path)
				continue;
			if (!ini_value(path, "Icon Theme", "Inherits", val, sizeof val)) continue;
			for (const char *p = val; *p;) {
				const char *e = strchr(p, ',');
				size_t n = e ? (size_t)(e - p) : strlen(p);
				push_theme(s, p, n);
				if (!e) break;
				p = e + 1;
			}
			break;
		}
	}
	push_theme(s, "hicolor", 7);
}

static int theme_rank(const struct ui_icon_scan *s, const char *theme) {
	for (size_t i = 0; i < s->n_themes; i++)
		if (strcmp(s->themes[i], theme) == 0) return (int)i;
	return OTHER_THEME_RANK;
}

static bool is_context(const char *name) {
	for (size_t i = 0; CONTEXTS[i]; i++)
		if (strncmp(name, CONTEXTS[i], strlen(CONTEXTS[i])) == 0) return true;
	return false;
}

static bool is_dir(const char *dir, const struct dirent *e) {
	if (e->d_type == DT_DIR) return true;
	if (e->d_type != DT_LNK && e->d_type != DT_UNKNOWN) return false;
	char p[PATH_CAP];
	struct stat sb;
	return snprintf(p, sizeof p, "%s/%s", dir, e->d_name) < (int)sizeof p && stat(p, &sb) == 0 &&
		   S_ISDIR(sb.st_mode);
}

static int size_of(const char *dir) {
	if (strcmp(dir, "scalable") == 0) return 0;
	return isdigit((unsigned char)dir[0]) ? atoi(dir) : -1;
}

static void scan_dir(struct ui_icons *ix, const char *dir, int depth, int base_rank, int size,
					 bool context) {
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		const char *dot = strrchr(e->d_name, '.');
		bool svg = dot && strcmp(dot, ".svg") == 0, png = dot && strcmp(dot, ".png") == 0;
		char path[PATH_CAP];
		if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
		if (svg || png) {
			if (!context) continue;
			int sz = size < 0 ? 32 : size;
			int dist = abs(sz == 0 ? 0 : sz - 32);
			int rank = base_rank * 1000 + (svg ? dist / 4 : 200 + dist);
			add_icon(ix, e->d_name, (size_t)(dot - e->d_name), path, rank);
		} else if (depth > 0 && is_dir(dir, e)) {
			int sz = size_of(e->d_name);
			scan_dir(ix, path, depth - 1, base_rank, sz >= 0 ? sz : size,
					 context || is_context(e->d_name));
		}
	}
	closedir(d);
}

struct ui_icon_scan *ui_icons_prepare(void) {
	struct ui_icon_scan *s = calloc(1, sizeof *s);
	if (!s) return NULL;
	find_bases(s);
	user_theme(s);
	add_inherits(s);
	return s;
}

void ui_icons_run(const struct ui_icon_scan *s, struct ui_icons *ix) {
	for (size_t b = 0; b < s->n_bases; b++) {
		DIR *d = opendir(s->bases[b]);
		if (!d) continue;
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.' || !is_dir(s->bases[b], e)) continue;
			char path[PATH_CAP];
			if (snprintf(path, sizeof path, "%s/%s", s->bases[b], e->d_name) >= (int)sizeof path)
				continue;
			scan_dir(ix, path, 3, theme_rank(s, e->d_name), -1, false);
		}
		closedir(d);
	}
	for (size_t p = 0; p < s->n_pixmaps; p++)
		scan_dir(ix, s->pixmaps[p], 0, PIXMAP_RANK, -1, true);
}

int ui_icons_scan(struct ui_icons *ix) {
	struct ui_icon_scan *s = ui_icons_prepare();
	if (!s) return -1;
	ui_icons_run(s, ix);
	free(s);
	return 0;
}

const char *ui_icons_path(const struct ui_icons *ix, const char *name) {
	size_t *slot = find_slot(ix, name, strlen(name));
	return slot && *slot ? ix->v[*slot - 1].path : NULL;
}

static int cmp_name(const void *a, const void *b) {
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int ui_icons_sorted_names(const struct ui_icons *ix, const char ***out) {
	const char **names = malloc((ix->n ? ix->n : 1) * sizeof *names);
	if (!names) return -1;
	for (size_t i = 0; i < ix->n; i++)
		names[i] = ix->v[i].name;
	qsort(names, ix->n, sizeof *names, cmp_name);
	*out = names;
	return 0;
}

void ui_icons_free(struct ui_icons *ix) {
	for (size_t i = 0; i < ix->n; i++) {
		free(ix->v[i].name);
		free(ix->v[i].path);
	}
	free(ix->v);
	free(ix->slots);
	*ix = (struct ui_icons){0};
}

static cairo_surface_t *render_svg(const char *path, int px) {
	NSVGimage *img = nsvgParseFromFile(path, "px", 96.0f);
	if (!img) return NULL;
	cairo_surface_t *out = NULL;
	float big = img->width > img->height ? img->width : img->height;
	NSVGrasterizer *rast = big > 0 ? nsvgCreateRasterizer() : NULL;
	unsigned char *rgba = rast ? malloc((size_t)px * (size_t)px * 4) : NULL;
	if (rgba) {
		float scale = (float)px / big;
		float tx = ((float)px - img->width * scale) / 2.0f;
		float ty = ((float)px - img->height * scale) / 2.0f;
		nsvgRasterize(rast, img, tx, ty, scale, rgba, px, px, px * 4);
		out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
		if (cairo_surface_status(out) == CAIRO_STATUS_SUCCESS) {
			cairo_surface_flush(out);
			unsigned char *dst = cairo_image_surface_get_data(out);
			int stride = cairo_image_surface_get_stride(out);
			for (int y = 0; y < px; y++) {
				uint32_t *row = (uint32_t *)(void *)(dst + (size_t)y * (size_t)stride);
				for (int x = 0; x < px; x++) {
					const unsigned char *p = rgba + ((size_t)y * (size_t)px + (size_t)x) * 4;
					uint32_t a = p[3];
					uint32_t r = p[0] * a / 255, g = p[1] * a / 255, b = p[2] * a / 255;
					row[x] = a << 24 | r << 16 | g << 8 | b;
				}
			}
			cairo_surface_mark_dirty(out);
		}
	}
	free(rgba);
	nsvgDeleteRasterizer(rast);
	nsvgDelete(img);
	return out;
}

static cairo_surface_t *render_png(const char *path, int px) {
	cairo_surface_t *src = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(src);
		return NULL;
	}
	int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src);
	cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
	cairo_t *cr = cairo_create(out);
	double big = w > h ? w : h;
	if (big > 0) {
		double sc = px / big;
		cairo_translate(cr, (px - w * sc) / 2.0, (px - h * sc) / 2.0);
		cairo_scale(cr, sc, sc);
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
	}
	cairo_destroy(cr);
	cairo_surface_destroy(src);
	return out;
}

cairo_surface_t *ui_icon_render(const char *path, int px) {
	if (!path || px <= 0) return NULL;
	const char *dot = strrchr(path, '.');
	if (dot && strcmp(dot, ".svg") == 0) return render_svg(path, px);
	if (dot && strcmp(dot, ".png") == 0) return render_png(path, px);
	return NULL;
}
