// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "capture/backend.h"
#include "paths.h"
#include "region/keybinds.h"
#include "region/region.h"
#include "upload/sxcu.h"
#include "util/util.h"
#include "wl/wl.h"

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct set_dep DEPS[] = {
	{"filename_preset", "filename", "", "Not used while a filename template is set"},
	{"recording.crf", "recording.format", "mp4,webm", "Only used for mp4 and webm recordings"},
	{"recording.preset", "recording.format", "mp4", "Only used for mp4 recordings"},
	{"recording.tune", "recording.format", "mp4", "Only used for mp4 recordings"},
	{"capture.hdr", "format", "png", "Only used for png screenshots"},
	{"png.level", "format", "png", "Only used for png screenshots"},
	{"jpeg.quality", "format", "jpeg", "Only used for jpeg screenshots"},
	{"webp.lossless", "format", "webp", "Only used for webp screenshots"},
	{"webp.quality", "webp.lossless", "false", "Not used while WebP lossless is on"},
	{"sound.player", "sound.enabled", "true", "Turn on Shutter sound to use this"},
	{"sound.file", "sound.enabled", "true", "Turn on Shutter sound to use this"},
	{"region.snap_animation", "region.window_snap", "true", "Turn on Snap to windows to use this"},
	{"preview.size", "preview.enabled", "true", "Turn on Preview card to use this"},
	{"preview.position", "preview.enabled", "true", "Turn on Preview card to use this"},
	{"preview.output", "preview.enabled", "true", "Turn on Preview card to use this"},
	{"preview.dismiss_secs", "preview.enabled", "true", "Turn on Preview card to use this"},
	{"translate.url", "translate.backend", "libretranslate,deepl", "Not used by trans"},
	{"translate.api_key", "translate.backend", "libretranslate,deepl", "Not used by trans"},
};

static void humanize(const char *key, char *out, size_t n) {
	const char *leaf = strrchr(key, '.');
	leaf = leaf ? leaf + 1 : key;
	snprintf(out, n, "%s", leaf);
	for (char *p = out; *p; p++)
		if (*p == '_') *p = ' ';
	if (out[0]) out[0] = (char)toupper((unsigned char)out[0]);
}

int set_page_push(struct set_page *p, struct set_row *r) {
	if (p->n == p->cap) {
		size_t cap = p->cap ? p->cap * 2 : 16;
		struct set_row **nr = realloc(p->rows, cap * sizeof *nr);
		if (!nr) return -1;
		p->rows = nr;
		p->cap = cap;
	}
	p->rows[p->n++] = r;
	return 0;
}

struct set_row *set_find_row(struct settings_state *st, const char *key) {
	for (size_t i = 0; i < st->all.n; i++)
		if (strcmp(st->all.rows[i]->key, key) == 0) return st->all.rows[i];
	return NULL;
}

static void clear_choices(struct set_row *r) {
	for (size_t i = 0; i < r->n_choices; i++)
		free(r->choices[i]);
	r->n_choices = 0;
}

int set_push_choice(struct set_row *r, const char *v) {
	if (r->n_choices == r->choices_cap) {
		size_t n = r->choices_cap ? r->choices_cap * 2 : 8;
		char **nc = realloc(r->choices, n * sizeof *nc);
		if (!nc) return -1;
		r->choices = nc;
		r->choices_cap = n;
	}
	char *d = strdup(v);
	if (!d) return -1;
	r->choices[r->n_choices++] = d;
	return 0;
}

static int add_choice(struct set_row *r, const char *v) {
	for (size_t i = 0; i < r->n_choices; i++)
		if (strcmp(r->choices[i], v) == 0) return 0;
	return set_push_choice(r, v);
}

static void add_uploaders(struct set_row *r) {
	char **names = NULL;
	size_t n = 0;
	if (sxcu_dir_list(&names, &n) != 0) return;
	for (size_t i = 0; i < n; i++) {
		add_choice(r, names[i]);
		free(names[i]);
	}
	free(names);
}

void set_fill_choices(struct settings_state *st, struct set_row *r) {
	if (r->info.kind != CFG_KIND_ENUM) return;
	if (r->info.values_src == CFG_VALUES_ICONS) return;
	for (size_t i = 0; r->info.values && r->info.values[i]; i++) {
		add_choice(r, r->info.values[i]);
	}
	switch (r->info.values_src) {
	case CFG_VALUES_FIXED:
		r->empty_label = "none";
		break;
	case CFG_VALUES_OUTPUTS:
		r->empty_label = "primary monitor";
		for (size_t i = 0; i < st->wls->n_outputs; i++) {
			const struct grabit_output *o = st->wls->outputs[i];
			if (!o->dead && o->name) add_choice(r, o->name);
		}
		break;
	case CFG_VALUES_UPLOADERS:
		r->empty_label = "not set";
		add_uploaders(r);
		break;
	case CFG_VALUES_ICONS:
		break;
	}
}

struct set_row *set_new_row(struct settings_state *st, const char *key,
							const char *label, const char *hint) {
	struct set_row *existing = set_find_row(st, key);
	if (existing) return existing;
	struct set_row *r = calloc(1, sizeof *r);
	if (!r) return NULL;
	r->key = key;
	r->hint = hint;
	if (config_key_info(key, &r->info) != 0) {
		free(r);
		return NULL;
	}
	if (label)
		snprintf(r->label, sizeof r->label, "%s", label);
	else
		humanize(key, r->label, sizeof r->label);
	set_fill_choices(st, r);
	if (r->info.secret) r->aux = AUX_EYE;
	if (r->info.kind == CFG_KIND_KEYBIND) r->aux = AUX_RECORD;
	if (strcmp(key, "edit.color") == 0) r->aux = AUX_SWATCH;
	for (size_t i = 0; i < sizeof DEPS / sizeof *DEPS; i++)
		if (strcmp(DEPS[i].key, key) == 0) r->dep = &DEPS[i];
	if (set_page_push(&st->all, r) != 0) {
		free(r);
		return NULL;
	}
	return r;
}

struct set_row *set_extra_row(struct settings_state *st, struct set_page *p, const char *key,
							  const char *label, const char *hint) {
	struct set_row *r = calloc(1, sizeof *r);
	if (!r) return NULL;
	r->key = key;
	snprintf(r->label, sizeof r->label, "%s", label);
	r->hint = hint;
	r->info.kind = CFG_KIND_TEXT;
	r->no_config = true;
	if (set_page_push(&st->all, r) != 0) {
		free(r);
		return NULL;
	}
	return set_page_push(p, r) == 0 ? r : NULL;
}

struct set_row *set_sxcu_row(struct settings_state *st) {
	return set_submit_row(st, set_add_sxcu);
}

static void free_row(struct set_row *r) {
	clear_choices(r);
	free(r->choices);
	free(r->owned_key);
	free(r->owned_hint);
	free(r->once_value);
	free(r);
}

static bool list_has(const struct set_row *r, const struct set_list *l, const char *name) {
	size_t pre = strlen(l->prefix), n = strlen(name);
	return strncmp(r->key, l->prefix, pre) == 0 && strncmp(r->key + pre, name, n) == 0 &&
		   strcmp(r->key + pre + n, l->suffix) == 0;
}

static void describe_row(struct settings_state *st, const struct set_list *l, struct set_row *r,
						 const char *name) {
	free(r->owned_hint);
	r->owned_hint = NULL;
	r->button = l->button;
	l->describe(st, r, name);
	r->hint = r->owned_hint ? r->owned_hint : l->unreadable;
}

static struct set_row *list_row(struct settings_state *st, const struct set_list *l,
								const char *name, const char *page) {
	struct set_row *r = calloc(1, sizeof *r);
	if (!r) return NULL;
	if (grabit_xasprintf(&r->owned_key, "%s%s%s", l->prefix, name, l->suffix) != 0) {
		free(r);
		return NULL;
	}
	r->key = r->owned_key;
	r->no_config = true;
	snprintf(r->label, sizeof r->label, "%s", name);
	r->info.kind = CFG_KIND_TEXT;
	r->section = l->section;
	r->page = page;
	r->action = l->action;
	for (size_t k = 0; k < SET_ALT_BTNS; k++)
		r->alt[k].action = l->alt[k];
	describe_row(st, l, r, name);
	return r;
}

static void drop_from_all(struct settings_state *st, const struct set_row *r) {
	size_t w = 0;
	for (size_t i = 0; i < st->all.n; i++)
		if (st->all.rows[i] != r) st->all.rows[w++] = st->all.rows[i];
	st->all.n = w;
}

static size_t index_of(const struct set_page *p, const struct set_row *r) {
	for (size_t i = 0; r && i < p->n; i++)
		if (p->rows[i] == r) return i;
	return SET_NONE;
}

static void sync_rows(struct settings_state *st, struct set_page *p, const struct set_list *l,
					  char **names, size_t n, struct set_row **keep[2]) {
	size_t start = 0;
	while (start < p->n && p->rows[start]->action != l->action)
		start++;
	struct set_row **fresh = calloc(n ? n : 1, sizeof *fresh);
	if (!fresh) return;
	for (size_t i = 0; i < n; i++) {
		for (size_t j = start; j < p->n && !fresh[i]; j++)
			if (p->rows[j] && list_has(p->rows[j], l, names[i])) {
				fresh[i] = p->rows[j];
				p->rows[j] = NULL;
				describe_row(st, l, fresh[i], names[i]);
			}
		if (fresh[i]) continue;
		fresh[i] = list_row(st, l, names[i], p->title);
		if (fresh[i] && set_page_push(&st->all, fresh[i]) != 0) {
			free_row(fresh[i]);
			fresh[i] = NULL;
		}
	}
	for (size_t j = start; j < p->n; j++) {
		struct set_row *gone = p->rows[j];
		if (!gone) continue;
		for (size_t k = 0; k < 2; k++)
			if (*keep[k] == gone) *keep[k] = NULL;
		drop_from_all(st, gone);
		free_row(gone);
	}
	p->n = start;
	for (size_t i = 0; i < n; i++) {
		if (!fresh[i] || set_page_push(p, fresh[i]) == 0) continue;
		drop_from_all(st, fresh[i]);
		free_row(fresh[i]);
	}
	free(fresh);
}

void set_sync_list(struct settings_state *st, size_t page, const struct set_list *l,
				   char **names, size_t n) {
	if (page >= st->n_pages) return;
	struct set_page *cur = set_current_page(st);
	struct set_row *focus = st->focus < cur->n ? cur->rows[st->focus] : NULL;
	struct set_row *popup = st->popup != POPUP_NONE && st->popup_row < cur->n
								? cur->rows[st->popup_row]
								: NULL;
	bool had_popup = popup != NULL;
	sync_rows(st, &st->pages[page], l, names, n, (struct set_row * *[2]){&focus, &popup});
	if (st->search.len > 0) settings_filter(st);
	cur = set_current_page(st);
	size_t f = index_of(cur, focus);
	if (f != SET_NONE)
		st->focus = f;
	else if (st->focus >= cur->n)
		st->focus = cur->n ? cur->n - 1 : 0;
	if (had_popup) {
		st->popup_row = index_of(cur, popup);
		if (st->popup_row == SET_NONE) {
			st->popup = POPUP_NONE;
			free(st->popup_arg);
			st->popup_arg = NULL;
		}
	}
	st->hover_row = st->press_row = st->tip_row = SET_NONE;
}

static void describe_uploader(struct settings_state *st, struct set_row *r, const char *name) {
	(void)st;
	struct sxcu_uploader u = {0};
	if (sxcu_dir_lookup(name, &u) != 0) return;
	if (u.request_url) grabit_xasprintf(&r->owned_hint, "uploads to %s", u.request_url);
	sxcu_free(&u);
}

static const struct set_list UPLOADER_LIST = {
	.action = set_remove_uploader_ask,
	.prefix = "",
	.suffix = ".sxcu",
	.section = "Installed uploaders",
	.button = "Remove",
	.unreadable = "could not read this uploader",
	.describe = describe_uploader,
};

void set_refresh_uploaders(struct settings_state *st) {
	for (size_t i = 0; i < st->all.n; i++) {
		struct set_row *r = st->all.rows[i];
		if (r->info.values_src != CFG_VALUES_UPLOADERS) continue;
		clear_choices(r);
		set_fill_choices(st, r);
	}
	char **names = NULL;
	size_t n = 0;
	if (sxcu_dir_list(&names, &n) != 0) return;
	set_sync_list(st, st->uploads_page, &UPLOADER_LIST, names, n);
	grabit_strv_free(names, n);
}

static void describe_unknown(struct settings_state *st, struct set_row *r, const char *name) {
	const char *v = config_get(&st->cfg, name);
	const char *val = v ? v : "";
	const char *hint = config_suggest_key(name);
	r->alt[0].label = hint ? "Rename" : NULL;
	if (hint)
		grabit_xasprintf(&r->owned_hint, "set to %s, but grabit has no such key; did you mean %s?",
						 val, hint);
	else
		grabit_xasprintf(&r->owned_hint, "set to %s, but grabit has no such key, so it does nothing",
						 val);
}

static const struct set_list UNKNOWN_LIST = {
	.action = set_remove_unknown_ask,
	.alt = {set_rename_unknown},
	.prefix = "",
	.suffix = "",
	.section = "Unknown keys in config.toml",
	.button = "Remove",
	.unreadable = "grabit has no such key",
	.describe = describe_unknown,
};

static void refresh_unknown(struct settings_state *st) {
	char **names = calloc(st->cfg.n + 1, sizeof *names);
	size_t n = 0;
	if (!names) return;
	for (size_t i = 0; i < st->cfg.n; i++) {
		if (config_key_known(st->cfg.kvs[i].key)) continue;
		names[n] = strdup(st->cfg.kvs[i].key);
		if (names[n]) n++;
	}
	if (n > 1) qsort(names, n, sizeof *names, grabit_cmp_str);
	set_sync_list(st, 0, &UNKNOWN_LIST, names, n);
	grabit_strv_free(names, n);
}

void set_refresh_cfg(struct settings_state *st) {
	refresh_unknown(st);
	set_refresh_notes(st);
}

static void add_note(struct settings_state *st, const char *key, const char *text) {
	struct set_row *r = set_find_row(st, key);
	if (!r) return;
	if (r->note[0])
		r->note_more++;
	else
		snprintf(r->note, sizeof r->note, "%s", text);
}

static const char *label_of(struct settings_state *st, const char *key) {
	struct set_row *r = set_find_row(st, key);
	return r ? r->label : key;
}

static bool kwin_offered(void) {
	static int known = -1;
	if (known < 0) known = grabit_desktop_is("KDE") && grabit_kwin_screenshot_available();
	return known;
}

static void backend_note(struct settings_state *st) {
	if (!st->wls) return;
	static const char *const NAMES[] = {"wlr", "ext", "kwin"};
	bool have[] = {st->wls->screencopy_manager != NULL,
				   st->wls->ext_copy_manager && st->wls->ext_source_manager, kwin_offered()};
	char list[32] = "";
	size_t off = 0;
	for (size_t i = 0; i < 3; i++)
		if (have[i]) grabit_join_appendf(list, sizeof list, &off, ", ", "%s", NAMES[i]);
	const char *want = config_get(&st->cfg, "capture.backend");
	char text[160];
	for (size_t i = 0; want && i < 3; i++)
		if (strcmp(want, NAMES[i]) == 0 && !have[i]) {
			snprintf(text, sizeof text, "This compositor does not offer %s; captures will fail%s%s",
					 want, list[0] ? ". It offers " : "", list);
			add_note(st, "capture.backend", text);
			return;
		}
	if (!list[0]) add_note(st, "capture.backend", "This compositor offers no capture protocol grabit can use");
}

void set_refresh_notes(struct settings_state *st) {
	for (size_t i = 0; i < st->all.n; i++) {
		st->all.rows[i]->note[0] = '\0';
		st->all.rows[i]->note_more = 0;
	}
	backend_note(st);
	struct region_keybind_clash c[64];
	size_t n = region_keymap_clashes(&st->cfg, c, 64);
	for (size_t i = 0; i < n; i++) {
		const char *la = label_of(st, c[i].a), *lb = label_of(st, c[i].b);
		char ta[160], tb[160];
		if (c[i].taken) {
			snprintf(ta, sizeof ta, "Takes %s from %s", c[i].bind, lb);
			snprintf(tb, sizeof tb, "%s taken by %s", c[i].bind, la);
		} else {
			snprintf(ta, sizeof ta, "Also used by %s (%s)", lb, c[i].bind);
			snprintf(tb, sizeof tb, "Also used by %s (%s)", la, c[i].bind);
		}
		add_note(st, c[i].a, ta);
		add_note(st, c[i].b, tb);
	}
	for (size_t i = 0; i < st->all.n; i++) {
		struct set_row *r = st->all.rows[i];
		size_t len = strlen(r->note);
		if (r->note_more && len < sizeof r->note)
			snprintf(r->note + len, sizeof r->note - len, " and %zu more", r->note_more);
	}
}

void settings_free_rows(struct settings_state *st) {
	for (size_t i = 0; i < st->all.n; i++) {
		free_row(st->all.rows[i]);
	}
	free(st->all.rows);
	for (size_t i = 0; i < st->n_pages; i++)
		free(st->pages[i].rows);
	free(st->results.rows);
	st->all = (struct set_page){0};
	st->n_pages = 0;
	st->results.rows = NULL;
}

static size_t split_words(char *q, const char *delims, char **words, size_t cap) {
	size_t n = 0;
	char *save = NULL;
	for (char *w = strtok_r(q, delims, &save); w && n < cap; w = strtok_r(NULL, delims, &save))
		words[n++] = w;
	return n;
}

static bool matches(const char *hay, const char *needle) {
	return hay && strcasestr(hay, needle);
}

static bool row_matches(const struct set_row *r, const char *word, const char *text) {
	if (text) return matches(text, word) || matches(r->hint, word) || matches(r->page, word);
	if (matches(r->key, word) || matches(r->label, word) || matches(r->hint, word) ||
		matches(r->page, word))
		return true;
	if (set_row_has_icons(r)) return false;
	for (size_t i = 0; i < r->n_choices; i++)
		if (matches(r->choices[i], word)) return true;
	return false;
}

void settings_filter(struct settings_state *st) {
	st->results.n = 0;
	char q[UI_TEXT_MAX];
	snprintf(q, sizeof q, "%s", st->search.buf);
	char *words[16];
	size_t n = split_words(q, " .\t", words, 16);
	if (n == 0) return;
	for (size_t i = 0; i < st->all.n; i++) {
		struct set_row *r = st->all.rows[i];
		if (r->custom || r->scratch) continue;
		size_t k = 0;
		const char *text = set_hist_row_text(st, r);
		while (k < n && row_matches(r, words[k], text))
			k++;
		if (k == n) set_page_push(&st->results, r);
	}
}

size_t set_page_matches(const struct settings_state *st, size_t page) {
	size_t n = 0;
	for (size_t i = 0; i < st->results.n; i++)
		if (st->results.rows[i]->page == st->pages[page].title) n++;
	return n;
}

struct set_page *set_current_page(struct settings_state *st) {
	if (st->search.len > 0) return &st->results;
	return &st->pages[st->page < st->n_pages ? st->page : 0];
}

const char *set_value(struct settings_state *st, const struct set_row *r) {
	if (r->scratch) return set_scratch_value(st, r);
	return r->no_config ? NULL : config_get(&st->cfg, r->key);
}

const char *set_effective(struct settings_state *st, const struct set_row *r) {
	const char *v = set_value(st, r);
	if (v) return v;
	return r->info.def ? r->info.def : "";
}

bool set_is_custom(struct settings_state *st, const struct set_row *r) {
	if (r->scratch) return r->once_value != NULL;
	const char *v = set_value(st, r);
	return v && (!r->info.def || strcmp(v, r->info.def) != 0);
}

bool set_can_reset(struct settings_state *st, const struct set_row *r) {
	if (set_override_file(st, r, NULL, 0)) return false;
	return set_is_custom(st, r) || (r->info.state && set_value(st, r));
}

bool set_step_enabled(struct settings_state *st, const struct set_row *r, int dir) {
	const char *cur = set_effective(st, r);
	if (!cur[0] || strcmp(cur, "auto") == 0) return dir > 0;
	long n = strtol(cur, NULL, 10);
	if (dir > 0) return n < r->info.hi;
	return n > r->info.lo || r->info.allow_auto;
}

static bool in_list(const char *list, const char *v) {
	size_t n = strlen(v);
	for (const char *p = list;;) {
		const char *e = strchr(p, ',');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		if (len == n && strncmp(p, v, n) == 0) return true;
		if (!e) return false;
		p = e + 1;
	}
}

const char *set_inactive_reason(struct settings_state *st, const struct set_row *r) {
	if (!r->dep) return NULL;
	struct set_row *other = set_find_row(st, r->dep->needs);
	if (!other) return NULL;
	const char *up = set_inactive_reason(st, other);
	if (up) return up;
	const char *v = config_alias_target(&other->info, set_effective(st, other));
	return in_list(r->dep->values, v) ? NULL : r->dep->reason;
}

void set_default_text(const struct set_row *r, char *out, size_t n) {
	const char *def = r->info.def ? r->info.def : "";
	if (r->info.kind == CFG_KIND_BOOL)
		snprintf(out, n, "%s", strcmp(def, "true") == 0 ? "on" : "off");
	else if (!def[0])
		snprintf(out, n, "%s", r->empty_label ? r->empty_label : "empty");
	else
		snprintf(out, n, "%s", def);
}

size_t set_dd_count(const struct set_row *r) {
	return r->n_choices + (r->info.allow_empty ? 1 : 0);
}

const char *set_dd_value(const struct set_row *r, size_t i) {
	if (r->info.allow_empty) {
		if (i == 0) return "";
		i--;
	}
	return i < r->n_choices ? r->choices[i] : "";
}

const char *set_dd_label(const struct set_row *r, size_t i) {
	const char *v = set_dd_value(r, i);
	for (size_t k = 0; v[0] && r->choice_labels && r->choice_labels[k] && k < r->n_choices; k++)
		if (strcmp(r->choices[k], v) == 0) return r->choice_labels[k];
	return v[0] ? v : r->empty_label ? r->empty_label
									 : "none";
}

size_t set_dd_index_of(struct settings_state *st, const struct set_row *r) {
	const char *v = config_alias_target(&r->info, set_effective(st, r));
	for (size_t i = 0; i < set_dd_count(r); i++)
		if (strcmp(set_dd_value(r, i), v) == 0) return i;
	if (!v[0] || !r->info.open_values || set_push_choice((struct set_row *)r, v) != 0) return 0;
	return set_dd_count(r) - 1;
}

void set_popup_refilter(struct settings_state *st) {
	struct set_page *p = set_current_page(st);
	st->popup_map_n = 0;
	if (st->popup_row >= p->n) return;
	struct set_row *r = p->rows[st->popup_row];
	size_t n = set_dd_count(r);
	if (st->popup_map_cap < n) {
		size_t *m = realloc(st->popup_map, n * sizeof *m);
		if (!m) return;
		st->popup_map = m;
		st->popup_map_cap = n;
	}
	char q[UI_TEXT_MAX];
	snprintf(q, sizeof q, "%s", st->popup_filter.buf);
	char *words[8];
	size_t nw = split_words(q, " \t", words, 8);
	for (size_t i = 0; i < n; i++) {
		size_t k = 0;
		const char *label = set_dd_label(r, i), *value = set_dd_value(r, i);
		while (k < nw && (matches(label, words[k]) || (value != label && matches(value, words[k]))))
			k++;
		if (k == nw) st->popup_map[st->popup_map_n++] = i;
	}
}
