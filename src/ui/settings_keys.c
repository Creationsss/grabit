// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "region/keybinds.h"
#include "region/region.h"
#include "upload/sxcu.h"
#include "util/util.h"
#include "wl/wl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct set_def {
	const char *key;
	const char *label;
	const char *hint;
};

static const struct set_def GENERAL[] = {
	{"default_action", "Default action", "what a plain `grabit` run does with the capture"},
	{"service", "Upload service", "where uploads go: a built-in service or a registered sxcu uploader"},
	{"format", "Image format", "file format for screenshots"},
	{"notifications", "Notifications", "desktop notifications on success; failures are always shown"},
	{"also_save", "Also save a copy", "keep a file in the save folder when copying or uploading"},
	{"save_dir", "Save folder", "absolute path or ~/...; empty uses XDG_PICTURES_DIR / XDG_VIDEOS_DIR"},
	{"filename", "Filename template", "strftime-style template; see `grabit help filename`"},
	{"filename_preset", "Filename preset", "used when no template is set"},
	{"save_state", "Remember editor state", "keep the last color, width, tool, swatches and region between runs"},
	{"log.enabled", "Log file", "mirror every log line to $XDG_RUNTIME_DIR/grabit.log"},
	{"sound.enabled", "Shutter sound", NULL},
	{"sound.player", "Sound player", "empty auto-detects pw-play, paplay, play or aplay"},
	{"sound.file", "Sound file", "empty auto-detects the freedesktop camera-shutter sound"},
	{NULL, NULL, NULL},
};

static const struct set_def CAPTURE[] = {
	{"capture.backend", "Capture backend", "auto tries wlr, then ext, then kwin"},
	{"capture.cursor", "Include the cursor", "draw the mouse pointer into screenshots"},
	{"capture.delay_secs", "Delay (seconds)", "wait before capturing so you can open a menu first"},
	{"capture.hdr", "Keep HDR", "write 16-bit png for hdr outputs instead of tone mapping to srgb"},
	{"png.level", "PNG compression", "0-9; higher is smaller but slower"},
	{"jpeg.quality", "JPEG quality", NULL},
	{"webp.quality", "WebP quality", "ignored when lossless is on"},
	{"webp.lossless", "WebP lossless", NULL},
	{NULL, NULL, NULL},
};

static const struct set_def REGION[] = {
	{"region.window_snap", "Snap to windows", "hover highlights windows; click captures one"},
	{"region.window_radius", "Window corner radius", "round window captures; auto matches the compositor"},
	{"region.window_borders", "Include window borders", "grow window captures by the compositor's border width"},
	{"region.snap_animation", "Animate the snap highlight", NULL},
	{"region.snap_ignore", "Ignore these windows", "comma-separated classes that snapping looks through"},
	{"region.confirm", "Confirm the selection", "keep the selection adjustable until Enter"},
	{"region.show_coords", "Show coordinates", "live cursor position readout while selecting"},
	{"region.magnifier_edits", "Magnifier shows edits", "draw annotations inside the magnifier"},
	{"region.repeat_last", "Reuse the last region", "skip the selector, same as -L"},
	{NULL, NULL, NULL},
};

static const struct set_def EDITOR[] = {
	{"edit.always", "Always open the editor", "same as passing -e on every capture"},
	{"edit.instant_capture", "Capture on select", "selecting a region captures it right away"},
	{"edit.start_with_tool", "Start with the last tool", "open in drawing mode instead of region-select"},
	{"edit.tool", "Tool", "the editor reopens with this tool"},
	{"edit.color", "Color", "#rrggbb, #rgb, or red/yellow/green/blue/black/white"},
	{"edit.width", "Stroke width", NULL},
	{"edit.swatches", "Swatches", "six comma-separated colors for the toolbar, each #rrggbb, #rgb, or red/yellow/green/blue/black/white"},
	{"edit.line_style", "Line style", "for pen, marker, line and shapes"},
	{"edit.smooth", "Smooth strokes", "fit pen, marker and eraser strokes to a curve"},
	{"edit.eyedropper_edits", "Eyedropper reads edits", "pick colors from the annotated image"},
	{"edit.multi_select", "Multi-select modifier", "hold it and click to add to the selection"},
	{"edit.toolbar_placement", "Toolbar placement", "attach puts it next to the selection"},
	{"edit.toolbar_output", "Toolbar output", "pin the toolbar to one monitor"},
	{NULL, NULL, NULL},
};

static const struct set_def RECORDING[] = {
	{"recording.format", "Format", "mp4 (h.264), webm (vp9) or gif"},
	{"recording.fps", "Frame rate", NULL},
	{"recording.crf", "Quality (crf)", "0-51, lower is better; mp4 and webm"},
	{"recording.preset", "Encoder preset", "slower presets give smaller files; mp4 only"},
	{"recording.tune", "Encoder tune", "mp4 only"},
	{"recording.pix_fmt", "Pixel format", NULL},
	{"recording.max_size_mb", "Max size (MB)", "re-encode when the file is larger; 0 turns it off"},
	{"recording.cursor", "Record the cursor", NULL},
	{"recording.show_dimensions", "Show dimensions", "size badge on the recording outline"},
	{"recording.tray", "Tray icon", "show a tray icon while recording"},
	{"recording.ffmpeg", "ffmpeg binary", NULL},
	{NULL, NULL, NULL},
};

static const struct set_def UPLOADS[] = {
	{"services.zipline.domain", "Zipline server", "https://host; /api/upload is added for you"},
	{"services.zipline.auth", "Zipline token", NULL},
	{"services.zipline.chunked", "Zipline chunked uploads", "always split uploads into chunks"},
	{"services.zipline.chunk_size", "Zipline chunk size (MiB)", NULL},
	{"services.nest.auth", "Nest token", NULL},
	{"services.nest.folder", "Nest folder", "optional folder uuid"},
	{"services.fakecrime.auth", "fakecrime token", NULL},
	{"services.ez.auth", "e-z token", NULL},
	{"services.guns.auth", "guns token", NULL},
	{"services.pixelvault.auth", "PixelVault token", NULL},
	{NULL, NULL, NULL},
};

static const struct set_def OCR[] = {
	{"ocr.lang", "OCR language", "tesseract -l value, e.g. eng or eng+deu"},
	{"ocr.tesseract", "tesseract binary", NULL},
	{"translate.target", "Translate to", "iso-639-1 code, e.g. en"},
	{"translate.backend", "Translator", NULL},
	{"translate.url", "Translator url", "required for libretranslate; optional for deepl, where it replaces the default api host"},
	{"translate.api_key", "Translator api key", "required for deepl; optional for libretranslate, for servers that need a key"},
	{NULL, NULL, NULL},
};

static const struct set_def CARDS[] = {
	{"preview.enabled", "Preview card", "show a thumbnail after copy, upload or save"},
	{"preview.size", "Preview width", NULL},
	{"preview.position", "Preview position", NULL},
	{"preview.output", "Preview output", NULL},
	{"preview.dismiss_secs", "Preview timeout (s)", "0 keeps it until the next capture"},
	{"text_card.position", "Text card position", "where OCR and translate results show"},
	{"text_card.output", "Text card output", NULL},
	{"text_card.dismiss_secs", "Text card timeout (s)", "0 keeps it until replaced"},
	{NULL, NULL, NULL},
};

static const struct set_def APPEARANCE[] = {
	{"gui.radius", "Corner radius", "rounds grabit's own ui; auto matches the compositor"},
	{"tray.icon", "Tray icon", "camera and screenshot icons come first; search the list for any icon in your themes"},
	{NULL, NULL, NULL},
};

struct set_dep {
	const char *key;
	const char *needs;
	const char *values;
	const char *reason;
};

static const struct set_dep DEPS[] = {
	{"recording.crf", "recording.format", "mp4,webm", "Only used for mp4 and webm recordings"},
	{"recording.preset", "recording.format", "mp4", "Only used for mp4 recordings"},
	{"recording.tune", "recording.format", "mp4", "Only used for mp4 recordings"},
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

struct page_def {
	const char *title;
	const struct set_def *defs;
	const char *note;
};

static const struct page_def PAGES[] = {
	{"General", GENERAL, NULL},
	{"Capture", CAPTURE, NULL},
	{"Region selector", REGION, NULL},
	{"Editor", EDITOR, "color, width, tool and swatches are remembered from your last edit"},
	{"Recording", RECORDING, NULL},
	{"Uploads", UPLOADS, "drop a .sxcu file anywhere on this window to add it as an uploader"},
	{"OCR and translate", OCR, NULL},
	{"Cards", CARDS, NULL},
	{"Appearance", APPEARANCE, NULL},
};

static void humanize(const char *key, char *out, size_t n) {
	const char *leaf = strrchr(key, '.');
	leaf = leaf ? leaf + 1 : key;
	snprintf(out, n, "%s", leaf);
	for (char *p = out; *p; p++)
		if (*p == '_') *p = ' ';
	if (out[0]) out[0] = (char)toupper((unsigned char)out[0]);
}

static int page_push(struct set_page *p, struct set_row *r) {
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

static struct set_row *find_row(struct settings_state *st, const char *key) {
	for (size_t i = 0; i < st->all.n; i++)
		if (strcmp(st->all.rows[i]->key, key) == 0) return st->all.rows[i];
	return NULL;
}

static void clear_choices(struct set_row *r) {
	for (size_t i = 0; i < r->n_choices; i++)
		free(r->choices[i]);
	r->n_choices = 0;
}

static int push_choice(struct set_row *r, const char *v) {
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
	return push_choice(r, v);
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

static void fill_choices(struct settings_state *st, struct set_row *r) {
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

static struct set_row *new_row(struct settings_state *st, const char *key,
							   const char *label, const char *hint) {
	struct set_row *existing = find_row(st, key);
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
	fill_choices(st, r);
	if (r->info.secret) r->aux = AUX_EYE;
	if (r->info.kind == CFG_KIND_KEYBIND) r->aux = AUX_RECORD;
	if (strcmp(key, "edit.color") == 0) r->aux = AUX_SWATCH;
	for (size_t i = 0; i < sizeof DEPS / sizeof *DEPS; i++)
		if (strcmp(DEPS[i].key, key) == 0) r->dep = &DEPS[i];
	if (page_push(&st->all, r) != 0) {
		free(r);
		return NULL;
	}
	return r;
}

static int add_sxcu_row(struct settings_state *st, struct set_page *p) {
	struct set_row *r = calloc(1, sizeof *r);
	if (!r) return -1;
	r->key = "grabit sxcu add";
	r->sxcu_add = true;
	snprintf(r->label, sizeof r->label, "Add uploader");
	r->hint = "path to a ShareX .sxcu file; it is copied into grabit's uploaders and can then be picked as the upload service";
	r->info.kind = CFG_KIND_TEXT;
	r->info.example = "~/Downloads/uploader.sxcu";
	if (page_push(&st->all, r) != 0) {
		free(r);
		return -1;
	}
	return page_push(p, r);
}

struct set_row *set_sxcu_row(struct settings_state *st) {
	for (size_t i = 0; i < st->all.n; i++)
		if (st->all.rows[i]->sxcu_add) return st->all.rows[i];
	return NULL;
}

static void free_row(struct set_row *r) {
	clear_choices(r);
	free(r->choices);
	free(r->owned_key);
	free(r->owned_hint);
	free(r);
}

static bool is_uploader(const struct set_row *r) {
	return r->action == set_remove_uploader_ask;
}

static bool uploader_is(const struct set_row *r, const char *name) {
	size_t n = strlen(name);
	return strncmp(r->key, name, n) == 0 && strcmp(r->key + n, ".sxcu") == 0;
}

static struct set_row *uploader_row(const char *name, const char *page) {
	struct set_row *r = calloc(1, sizeof *r);
	if (!r) return NULL;
	if (grabit_xasprintf(&r->owned_key, "%s.sxcu", name) != 0) {
		free(r);
		return NULL;
	}
	struct sxcu_uploader u = {0};
	if (sxcu_dir_lookup(name, &u) == 0) {
		if (u.request_url) grabit_xasprintf(&r->owned_hint, "uploads to %s", u.request_url);
		sxcu_free(&u);
	}
	r->key = r->owned_key;
	r->hint = r->owned_hint ? r->owned_hint : "could not read this uploader";
	snprintf(r->label, sizeof r->label, "%s", name);
	r->info.kind = CFG_KIND_TEXT;
	r->section = "Installed uploaders";
	r->page = page;
	r->button = "Remove";
	r->action = set_remove_uploader_ask;
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

static void sync_uploader_rows(struct settings_state *st, struct set_page *p, char **names,
							   size_t n, struct set_row **keep[2]) {
	size_t start = 0;
	while (start < p->n && !is_uploader(p->rows[start]))
		start++;
	struct set_row **fresh = calloc(n ? n : 1, sizeof *fresh);
	if (!fresh) return;
	for (size_t i = 0; i < n; i++) {
		for (size_t j = start; j < p->n && !fresh[i]; j++)
			if (p->rows[j] && uploader_is(p->rows[j], names[i])) {
				fresh[i] = p->rows[j];
				p->rows[j] = NULL;
			}
		if (fresh[i]) continue;
		fresh[i] = uploader_row(names[i], p->title);
		if (fresh[i] && page_push(&st->all, fresh[i]) != 0) {
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
		if (!fresh[i] || page_push(p, fresh[i]) == 0) continue;
		drop_from_all(st, fresh[i]);
		free_row(fresh[i]);
	}
	free(fresh);
}

void set_refresh_uploaders(struct settings_state *st) {
	for (size_t i = 0; i < st->all.n; i++) {
		struct set_row *r = st->all.rows[i];
		if (r->info.values_src != CFG_VALUES_UPLOADERS) continue;
		clear_choices(r);
		fill_choices(st, r);
	}
	char **names = NULL;
	size_t n = 0;
	if (st->uploads_page >= st->n_pages || sxcu_dir_list(&names, &n) != 0) return;
	struct set_page *cur = set_current_page(st);
	struct set_row *focus = st->focus < cur->n ? cur->rows[st->focus] : NULL;
	struct set_row *popup = st->popup != POPUP_NONE && st->popup_row < cur->n
								? cur->rows[st->popup_row]
								: NULL;
	bool had_popup = popup != NULL;
	sync_uploader_rows(st, &st->pages[st->uploads_page], names, n,
					   (struct set_row * *[2]){&focus, &popup});
	for (size_t i = 0; i < n; i++)
		free(names[i]);
	free(names);
	if (st->search.len > 0) settings_filter(st);
	cur = set_current_page(st);
	size_t f = index_of(cur, focus);
	if (f != SET_NONE)
		st->focus = f;
	else if (st->focus >= cur->n)
		st->focus = cur->n ? cur->n - 1 : 0;
	if (had_popup) {
		st->popup_row = index_of(cur, popup);
		if (st->popup_row == SET_NONE) st->popup = POPUP_NONE;
	}
	st->hover_row = st->press_row = st->tip_row = SET_NONE;
}

static void add_note(struct settings_state *st, const char *key, const char *text) {
	struct set_row *r = find_row(st, key);
	if (!r) return;
	if (r->note[0])
		r->note_more++;
	else
		snprintf(r->note, sizeof r->note, "%s", text);
}

static const char *label_of(struct settings_state *st, const char *key) {
	struct set_row *r = find_row(st, key);
	return r ? r->label : key;
}

void set_refresh_notes(struct settings_state *st) {
	for (size_t i = 0; i < st->all.n; i++) {
		st->all.rows[i]->note[0] = '\0';
		st->all.rows[i]->note_more = 0;
	}
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

struct leftover_ctx {
	struct settings_state *st;
	struct set_page *page;
	int rc;
};

static void add_leftover(const char *key, void *data) {
	struct leftover_ctx *c = data;
	if (c->rc != 0 || find_row(c->st, key)) return;
	struct config_key_info info;
	if (config_key_info(key, &info) != 0 || info.internal) return;
	struct set_row *r = new_row(c->st, key, NULL, NULL);
	if (r && page_push(c->page, r) != 0) c->rc = -1;
}

struct action_def {
	enum region_action act;
	const char *label;
	const char *section;
};

static const struct action_def ACTIONS[] = {
	{KA_CONFIRM, "Confirm", "General"},
	{KA_CANCEL, "Cancel", "General"},
	{KA_MAGNIFIER, "Magnifier", "General"},
	{KA_SELECT_ALL, "Select all", "Selection"},
	{KA_REGION_MODE, "Select region", "Selection"},
	{KA_EDIT_MODE, "Move/resize annotations", "Selection"},
	{KA_UNDO, "Undo", "History"},
	{KA_REDO, "Redo", "History"},
	{KA_DELETE, "Delete", "History"},
	{KA_NUDGE_LEFT, "Nudge left", "Nudge"},
	{KA_NUDGE_RIGHT, "Nudge right", "Nudge"},
	{KA_NUDGE_UP, "Nudge up", "Nudge"},
	{KA_NUDGE_DOWN, "Nudge down", "Nudge"},
};

static int push_bind(struct settings_state *st, struct set_page *p, const char *key,
					 const char *label, const char *section) {
	if (!key) return 0;
	struct set_row *r = new_row(st, key, label, NULL);
	if (!r) return 0;
	r->section = section;
	return page_push(p, r);
}

static int build_keybinds(struct settings_state *st, struct set_page *p) {
	p->title = "Keybinds";
	p->note = "comma-separated, e.g. \"Return, Ctrl+s\" or \"mouse:right\"; the record button captures them live";
	bool done[KA_COUNT] = {0};
	for (size_t i = 0; i < sizeof ACTIONS / sizeof *ACTIONS; i++) {
		done[ACTIONS[i].act] = true;
		if (push_bind(st, p, region_keybind_action_key(ACTIONS[i].act), ACTIONS[i].label,
					  ACTIONS[i].section) != 0)
			return -1;
	}
	for (int a = 0; a < KA_COUNT; a++)
		if (!done[a] && push_bind(st, p, region_keybind_action_key(a), NULL, "General") != 0)
			return -1;
	for (int t = 0; t < TOOL_COUNT; t++) {
		const char *key = region_keybind_tool_key(t);
		if (!region_keybind_default(key)) continue;
		const char *section = region_tool_group_label(t);
		if (push_bind(st, p, key, region_tool_label(t), section ? section : "Other tools") != 0)
			return -1;
	}
	return 0;
}

int settings_build(struct settings_state *st) {
	for (size_t i = 0; i < sizeof PAGES / sizeof *PAGES; i++) {
		struct set_page *p = &st->pages[st->n_pages++];
		p->title = PAGES[i].title;
		p->note = PAGES[i].note;
		if (PAGES[i].defs == UPLOADS && add_sxcu_row(st, p) != 0) return -1;
		for (const struct set_def *d = PAGES[i].defs; d->key; d++) {
			struct set_row *r = new_row(st, d->key, d->label, d->hint);
			if (r && page_push(p, r) != 0) return -1;
		}
		if (PAGES[i].defs == UPLOADS) st->uploads_page = st->n_pages - 1;
	}
	if (build_keybinds(st, &st->pages[st->n_pages++]) != 0) return -1;

	struct set_page *other = &st->pages[st->n_pages];
	other->title = "Other";
	struct leftover_ctx c = {.st = st, .page = other};
	config_each_key(add_leftover, &c);
	if (c.rc != 0) return -1;
	if (other->n > 0) st->n_pages++;
	for (size_t i = 0; i < st->n_pages; i++)
		for (size_t j = 0; j < st->pages[i].n; j++)
			if (!st->pages[i].rows[j]->page) st->pages[i].rows[j]->page = st->pages[i].title;
	st->results.title = "Search";
	set_refresh_uploaders(st);
	set_refresh_notes(st);
	return 0;
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

static bool row_matches(const struct set_row *r, const char *word) {
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
		size_t k = 0;
		while (k < n && row_matches(r, words[k]))
			k++;
		if (k == n) page_push(&st->results, r);
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
	return config_get(&st->cfg, r->key);
}

const char *set_effective(struct settings_state *st, const struct set_row *r) {
	const char *v = set_value(st, r);
	if (v) return v;
	return r->info.def ? r->info.def : "";
}

bool set_is_custom(struct settings_state *st, const struct set_row *r) {
	const char *v = set_value(st, r);
	if (!v) return false;
	if (r->info.def) return strcmp(v, r->info.def) != 0;
	return v[0] != '\0';
}

bool set_can_reset(struct settings_state *st, const struct set_row *r) {
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
	for (const char *p = list; *p;) {
		const char *e = strchr(p, ',');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		if (len == n && strncmp(p, v, n) == 0) return true;
		if (!e) break;
		p = e + 1;
	}
	return false;
}

const char *set_inactive_reason(struct settings_state *st, const struct set_row *r) {
	if (!r->dep) return NULL;
	struct set_row *other = find_row(st, r->dep->needs);
	if (!other || in_list(r->dep->values, set_effective(st, other))) return NULL;
	return r->dep->reason;
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
	return v[0] ? v : r->empty_label ? r->empty_label
									 : "none";
}

size_t set_dd_index_of(struct settings_state *st, const struct set_row *r) {
	const char *v = config_alias_target(&r->info, set_effective(st, r));
	for (size_t i = 0; i < set_dd_count(r); i++)
		if (strcmp(set_dd_value(r, i), v) == 0) return i;
	if (!v[0] || !r->info.open_values || push_choice((struct set_row *)r, v) != 0) return 0;
	return set_dd_count(r) - 1;
}

bool set_row_has_icons(const struct set_row *r) {
	return r->info.kind == CFG_KIND_ENUM && r->info.values_src == CFG_VALUES_ICONS;
}

static void *scan_thread(void *data) {
	struct settings_state *st = data;
	ui_icons_run(st->icon_scan, &st->icons);
	atomic_store_explicit(&st->icons_done, true, memory_order_release);
	return NULL;
}

void set_icons_start(struct settings_state *st) {
	if (st->icons_state != ICONS_IDLE) return;
	st->icon_scan = ui_icons_prepare();
	if (!st->icon_scan) return;
	atomic_store(&st->icons_done, false);
	if (pthread_create(&st->icons_thread, NULL, scan_thread, st) == 0)
		st->icons_state = ICONS_SCANNING;
}

static void icons_join(struct settings_state *st) {
	if (st->icons_state != ICONS_SCANNING) return;
	pthread_join(st->icons_thread, NULL);
	st->icons_state = ICONS_READY;
	free(st->icon_scan);
	st->icon_scan = NULL;
}

bool set_icons_poll(struct settings_state *st) {
	if (st->icons_state == ICONS_SCANNING &&
		atomic_load_explicit(&st->icons_done, memory_order_acquire))
		icons_join(st);
	return st->icons_state == ICONS_READY;
}

static void icons_wait(struct settings_state *st) {
	if (st->icons_state == ICONS_READY) return;
	if (st->icons_state == ICONS_SCANNING) {
		icons_join(st);
		return;
	}
	ui_icons_scan(&st->icons);
	st->icons_state = ICONS_READY;
}

static bool already_listed(const struct set_row *r, size_t n, const char *name) {
	for (size_t i = 0; i < n; i++)
		if (strcmp(r->choices[i], name) == 0) return true;
	return false;
}

bool set_icons_ensure(struct settings_state *st, struct set_row *r, bool block) {
	if (!set_row_has_icons(r) || r->icons_loaded) return true;
	if (block)
		icons_wait(st);
	else if (!set_icons_poll(st))
		return false;
	r->icons_loaded = true;
	size_t pre = r->n_choices;
	for (size_t i = 0; r->info.values[i]; i++) {
		const char *v = r->info.values[i];
		if (!ui_icons_path(&st->icons, v) || already_listed(r, pre, v)) continue;
		if (push_choice(r, v) != 0) return true;
	}
	const char **names = NULL;
	if (ui_icons_sorted_names(&st->icons, &names) != 0) return true;
	for (size_t i = 0; i < st->icons.n; i++) {
		if (cfg_in_list(names[i], r->info.values) || already_listed(r, pre, names[i])) continue;
		if (push_choice(r, names[i]) != 0) break;
	}
	free(names);
	return true;
}

cairo_surface_t *set_icon(struct settings_state *st, const char *name, int px) {
	if (st->icons_state != ICONS_READY || !name || !name[0]) return NULL;
	for (size_t i = 0; i < SET_ICON_CACHE; i++) {
		struct set_icon_slot *c = &st->icon_cache[i];
		if (c->name && c->px == px && strcmp(c->name, name) == 0) return c->surface;
	}
	struct set_icon_slot *c = &st->icon_cache[st->icon_cache_next];
	st->icon_cache_next = (st->icon_cache_next + 1) % SET_ICON_CACHE;
	if (c->surface) cairo_surface_destroy(c->surface);
	c->name = name;
	c->px = px;
	c->surface = ui_icon_render(ui_icons_path(&st->icons, name), px);
	return c->surface;
}

void set_icons_free(struct settings_state *st) {
	icons_join(st);
	free(st->icon_scan);
	st->icon_scan = NULL;
	for (size_t i = 0; i < SET_ICON_CACHE; i++)
		if (st->icon_cache[i].surface) cairo_surface_destroy(st->icon_cache[i].surface);
	memset(st->icon_cache, 0, sizeof st->icon_cache);
	ui_icons_free(&st->icons);
	st->icons_state = ICONS_IDLE;
	free(st->popup_map);
	st->popup_map = NULL;
	st->popup_map_n = st->popup_map_cap = 0;
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
