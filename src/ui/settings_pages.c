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

struct set_def {
	const char *key;
	const char *label;
	const char *hint;
};

static const struct set_def GENERAL[] = {
	{"default_action", "Default action", "what a plain `grabit` run does with the capture"},
	{"notifications", "Notifications", "desktop notifications on success; failures are always shown"},
	{"also_save", "Also save a copy", "keep a file in the save folder when copying or uploading"},
	{"save_dir", "Save folder", "absolute path or ~/...; empty uses XDG_PICTURES_DIR / XDG_VIDEOS_DIR"},
	{"filename_preset", "Filename preset", "used when no template is set"},
	{"filename", "Filename template", "strftime-style template; see `grabit help filename`"},
	{"log.enabled", "Log file", "mirror every log line to $XDG_RUNTIME_DIR/grabit.log"},
	{"sound.enabled", "Shutter sound", NULL},
	{"sound.player", "Sound player", "empty auto-detects pw-play, paplay, play or aplay"},
	{"sound.file", "Sound file", "empty auto-detects the freedesktop camera-shutter sound"},
	{"history.enabled", "Keep history", "remember uploads, saved screenshots and recordings for the History tab and grabit history"},
	{"history.ocr", "Keep OCR history", "remember the text of each OCR run for the OCR history tab"},
	{"history.confirm_delete", "Ask before deleting", "show a dialog before deleting an upload or a saved file from the history"},
	{NULL, NULL, NULL},
};

static const struct set_def CAPTURE[] = {
	{"capture.backend", "Capture backend", "auto tries wlr, then ext, then kwin"},
	{"capture.cursor", "Include the cursor", "draw the mouse pointer into screenshots"},
	{"capture.delay_secs", "Delay (seconds)", "wait before capturing so you can open a menu first"},
	{"capture.hdr", "Keep HDR", "write 16-bit png for hdr outputs instead of tone mapping to srgb"},
	{"format", "Image format", "file format for screenshots"},
	{"png.level", "PNG compression", "0-9; higher is smaller but slower"},
	{"jpeg.quality", "JPEG quality", NULL},
	{"webp.lossless", "WebP lossless", NULL},
	{"webp.quality", "WebP quality", "ignored when lossless is on"},
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
	{"save_state", "Remember editor state", "keep the last color, width, tool, swatches, region, and the settings window size and page between runs"},
	{"edit.always", "Always open the editor", "same as passing -e on every capture"},
	{"edit.mode", "Editor mode", "window opens the editor in a normal window you can move, resize and leave open"},
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
	{"service", "Upload service", "where uploads go: a built-in service or a registered sxcu uploader"},
	{"services.zipline.domain", "Zipline server", "https://host; /api/upload is added for you"},
	{"services.zipline.auth", "Zipline token", NULL},
	{"services.zipline.chunked", "Zipline chunked uploads", "always split uploads into chunks"},
	{"services.zipline.chunk_size", "Zipline chunk size (MiB)", NULL},
	{"services.zipline.headers.x-zipline-format", "Zipline name format", "how zipline names uploads; grabit sends name when unset, keeping your filename"},
	{"services.zipline.headers.x-zipline-deletes-at", "Zipline deletes after", "e.g. 1d or 30m"},
	{"services.zipline.headers.x-zipline-max-views", "Zipline max views", NULL},
	{"services.zipline.headers.x-zipline-image-compression-type", "Zipline compression format", NULL},
	{"services.zipline.headers.x-zipline-image-compression-percent", "Zipline compression percent", NULL},
	{"services.zipline.headers.x-zipline-password", "Zipline password", "viewers need it to open the upload"},
	{"services.zipline.headers.x-zipline-folder", "Zipline folder", "folder id"},
	{"services.zipline.headers.x-zipline-domain", "Zipline link domain", "domain used in the returned link"},
	{"services.zipline.headers.x-zipline-file-extension", "Zipline file extension", NULL},
	{"services.zipline.headers.x-zipline-original-name", "Zipline keep original name", NULL},
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
	{"gui.radius", "Corner radius", "rounds grabit's own ui, including this window; auto matches the compositor"},
	{"tray.icon", "Tray icon", "hand, camera and screenshot icons come first; search the list for any icon in your themes, or type the path of a .png or .svg and press Enter"},
	{NULL, NULL, NULL},
};

static const struct set_def PLUGINS[] = {
	{"plugins.confirm_remove", "Ask before uninstalling", "show a dialog before uninstalling a plugin"},
	{0},
};

static const struct set_def NO_DEFS[] = {
	{0},
};

static const struct set_drop_hint DROP_UPLOADS = {
	"Drop to add an uploader", "ShareX .sxcu files are copied into grabit's uploaders",
	"Only ShareX .sxcu files can be added as uploaders"};
static const struct set_drop_hint DROP_PLUGINS = {
	"Drop to install a plugin", "a folder with the plugin's git repo and manifest.toml",
	"Drop a folder with a plugin's repo to install it"};
static const struct set_drop_hint DROP_ICON = {
	"Drop to set the tray icon", "a .png or .svg image",
	"Drop a .png or .svg image to use it as the tray icon"};
static const struct set_drop_hint DROP_FILE = {
	"Drop files to upload", "they are added below to review; nothing is uploaded until you press Upload",
	"Drop files to upload them"};
static const struct set_drop_hint DROP_ANY = {
	"Drop an uploader, a plugin or an icon",
	".sxcu files become uploaders, plugin repo folders are installed, and a .png or .svg becomes "
	"the tray icon",
	"Drop .sxcu files, a plugin folder, or a .png or .svg for the tray icon"};

const struct set_drop_hint *set_drop_hint(const struct settings_state *st) {
	const struct set_page *p = &st->pages[st->page];
	return st->page < st->n_pages && p->drop ? p->drop : &DROP_ANY;
}

struct page_def {
	const char *title;
	const struct set_def *defs;
	const char *note;
	const struct set_drop_hint *drop;
	int (*extra)(struct settings_state *st, struct set_page *p);
	size_t index_at;
	bool bottom;
};

static int add_config_row(struct settings_state *st, struct set_page *p);
static int add_general_rows(struct settings_state *st, struct set_page *p);
static int add_upload_rows(struct settings_state *st, struct set_page *p);
static int add_update_row(struct settings_state *st, struct set_page *p);

static const struct page_def PAGES[] = {
	{.title = "General", .defs = GENERAL, .extra = add_general_rows},
	{.title = "Capture", .defs = CAPTURE},
	{.title = "Region selector", .defs = REGION},
	{.title = "Editor", .defs = EDITOR, .note = "while Remember editor state is on, color, width, tool and swatches come from your last edit"},
	{.title = "Recording", .defs = RECORDING},
	{.title = "Uploads",
	 .defs = UPLOADS,
	 .note = "drop a .sxcu file anywhere on this window to add it as an uploader",
	 .drop = &DROP_UPLOADS,
	 .extra = add_upload_rows,
	 .index_at = offsetof(struct settings_state, uploads_page)},
	{.title = "Plugins",
	 .defs = PLUGINS,
	 .note = "install from a git URL below, or with grabit plugin add <git-url>",
	 .extra = add_update_row,
	 .drop = &DROP_PLUGINS,
	 .index_at = offsetof(struct settings_state, plugins_page)},
	{.title = "OCR and translate", .defs = OCR},
	{.title = "Cards", .defs = CARDS},
	{.title = "Appearance",
	 .defs = APPEARANCE,
	 .note = "drop a .png or .svg on this window to use it as the tray icon",
	 .drop = &DROP_ICON},
	{.title = "Upload",
	 .defs = NO_DEFS,
	 .note = "upload any file; the options apply to these uploads only and never change your settings",
	 .drop = &DROP_FILE,
	 .extra = set_add_upload_rows,
	 .index_at = offsetof(struct settings_state, upload_page),
	 .bottom = true},
	{.title = "History",
	 .defs = NO_DEFS,
	 .extra = set_add_history_rows,
	 .index_at = offsetof(struct settings_state, history_page),
	 .bottom = true},
	{.title = "OCR history",
	 .defs = NO_DEFS,
	 .note = "the text of each OCR run; the sidebar search finds text in it too",
	 .index_at = offsetof(struct settings_state, ocr_history_page),
	 .bottom = true},
};

static void tilde_path(const char *path, char *out, size_t n) {
	const char *home = getenv("HOME");
	size_t hl = home ? strlen(home) : 0;
	if (path && hl > 1 && strncmp(path, home, hl) == 0 && path[hl] == '/')
		snprintf(out, n, "~%s", path + hl);
	else
		snprintf(out, n, "%s", path ? path : "");
}

bool set_override_file(struct settings_state *st, const struct set_row *r, char *out, size_t n) {
	const char *path = r->no_config ? NULL : config_source(&st->cfg, r->key);
	if (!path || !out) return path != NULL;
	const char *dir = paths_config_dir();
	size_t dl = dir ? strlen(dir) : 0;
	if (dl && strncmp(path, dir, dl) == 0 && path[dl] == '/')
		snprintf(out, n, "%s", path + dl + 1);
	else
		tilde_path(path, out, n);
	return true;
}

static void open_config(struct settings_state *st, struct set_row *r) {
	(void)r;
	const char *path = paths_config_file();
	char shown[PATH_MAX];
	tilde_path(paths_config_file(), shown, sizeof shown);
	if (!path || paths_mkdir_p(paths_config_dir()) != 0) {
		set_toast(st, "Could not find the config folder");
		return;
	}
	int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
	if (fd >= 0) close(fd);
	int rc = grabit_xdg_open(path);
	if (rc == -2)
		set_toast(st, "xdg-open is not installed; the config is at %s", shown);
	else if (rc < 0)
		set_toast(st, "Could not start xdg-open");
	else
		set_toast(st, "Opened %s", shown);
}

static int add_config_row(struct settings_state *st, struct set_page *p) {
	char shown[PATH_MAX];
	tilde_path(paths_config_file(), shown, sizeof shown);
	char *key = strdup(shown);
	if (!key) return -1;
	struct set_row *r = set_extra_row(st, p, key, "Config file",
									  "opens in your default editor; saved edits show up here");
	if (!r) return -1;
	r->owned_key = key;
	r->button = "Open";
	r->action = open_config;
	return 0;
}

static int add_general_rows(struct settings_state *st, struct set_page *p) {
	size_t first = p->n;
	for (size_t i = 0; i < p->n; i++)
		if (strncmp(p->rows[i]->key, "history.", 8) == 0 && first == p->n) first = i;
	if (add_config_row(st, p) != 0) return -1;
	struct set_row *config = p->rows[p->n - 1];
	memmove(&p->rows[first + 1], &p->rows[first], (p->n - 1 - first) * sizeof *p->rows);
	p->rows[first++] = config;
	if (set_add_history_clear_rows(st, p) != 0) return -1;
	for (size_t i = first; i < p->n; i++)
		if (strncmp(p->rows[i]->key, "history.", 8) == 0 || strncmp(p->rows[i]->key, "grabit history", 14) == 0)
			p->rows[i]->section = "History";
	return 0;
}

static int add_upload_rows(struct settings_state *st, struct set_page *p) {
	struct set_row *t = set_extra_row(st, p, "grabit upload test", "Test upload",
									  "uploads a small test image to the upload service, then deletes it again where the service allows it");
	if (!t) return -1;
	t->button = "Test";
	t->action = set_test_upload;
	struct set_row *r = set_extra_row(st, p, "grabit sxcu add", "Add uploader",
									  "path to a ShareX .sxcu file; it is copied into grabit's uploaders and can then be picked as the upload service");
	if (!r) return -1;
	r->submit = set_add_sxcu;
	r->info.example = "~/Downloads/uploader.sxcu";
	return 0;
}

static int add_update_row(struct settings_state *st, struct set_page *p) {
	struct set_row *in = set_extra_row(st, p, "grabit plugin add", "Install plugin",
									   "a git URL, an ssh address such as git@host:user/repo, or a local "
									   "folder with the plugin's repo (or drop the folder on this window); "
									   "grabit clones and builds it, which can take a while");
	if (!in) return -1;
	in->submit = set_install_plugin;
	in->info.example = "https://github.com/<user>/<plugin>";
	struct set_row *r = set_extra_row(st, p, "grabit plugin update", "Update plugins",
									  "pulls and rebuilds each plugin, or fetches its new release; this can take a while");
	if (!r) return -1;
	r->button = SET_UPDATE_ALL;
	r->action = set_update_plugins;
	return 0;
}

struct set_row *set_submit_row(struct settings_state *st,
							   bool (*fn)(struct settings_state *st, const char *value)) {
	for (size_t i = 0; i < st->all.n; i++)
		if (st->all.rows[i]->submit == fn) return st->all.rows[i];
	return NULL;
}

struct leftover_ctx {
	struct settings_state *st;
	struct set_page *page;
	int rc;
};

static void add_leftover(const char *key, void *data) {
	struct leftover_ctx *c = data;
	if (c->rc != 0 || set_find_row(c->st, key)) return;
	struct config_key_info info;
	if (config_key_info(key, &info) != 0 || info.internal) return;
	struct set_row *r = set_new_row(c->st, key, NULL, NULL);
	if (r && set_page_push(c->page, r) != 0) c->rc = -1;
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
	struct set_row *r = set_new_row(st, key, label, NULL);
	if (!r) return 0;
	r->section = section;
	return set_page_push(p, r);
}

static int build_keybinds(struct settings_state *st, struct set_page *p) {
	p->title = "Keybinds";
	p->note = "comma-separated, e.g. \"Enter, Ctrl+s\" or \"mouse:right\"; the record button captures them live";
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

static int build_pages(struct settings_state *st, bool bottom) {
	for (size_t i = 0; i < sizeof PAGES / sizeof *PAGES; i++) {
		if (PAGES[i].bottom != bottom) continue;
		struct set_page *p = &st->pages[st->n_pages++];
		p->title = PAGES[i].title;
		p->note = PAGES[i].note;
		p->drop = PAGES[i].drop;
		p->bottom = bottom;
		if (PAGES[i].index_at) *(size_t *)((char *)st + PAGES[i].index_at) = st->n_pages - 1;
		for (const struct set_def *d = PAGES[i].defs; d->key; d++) {
			struct set_row *r = set_new_row(st, d->key, d->label, d->hint);
			if (r && set_page_push(p, r) != 0) return -1;
		}
		if (PAGES[i].extra && PAGES[i].extra(st, p) != 0) return -1;
	}
	return 0;
}

int settings_build(struct settings_state *st) {
	if (build_pages(st, false) != 0) return -1;
	if (build_keybinds(st, &st->pages[st->n_pages++]) != 0) return -1;

	size_t other_at = st->n_pages++;
	if (build_pages(st, true) != 0) return -1;
	struct set_page *other = &st->pages[other_at];
	other->title = "Other";
	struct leftover_ctx c = {.st = st, .page = other};
	config_each_key(add_leftover, &c);
	if (c.rc != 0) return -1;
	if (other->n == 0) {
		free(other->rows);
		memmove(other, other + 1, (st->n_pages - other_at - 1) * sizeof *other);
		st->n_pages--;
		st->pages[st->n_pages] = (struct set_page){0};
		for (size_t i = 0; i < sizeof PAGES / sizeof *PAGES; i++)
			if (PAGES[i].bottom && PAGES[i].index_at) (*(size_t *)((char *)st + PAGES[i].index_at))--;
	}
	for (size_t i = 0; i < st->n_pages; i++)
		for (size_t j = 0; j < st->pages[i].n; j++)
			if (!st->pages[i].rows[j]->page) st->pages[i].rows[j]->page = st->pages[i].title;
	st->results.title = "Search";
	set_refresh_uploaders(st);
	set_refresh_plugins(st);
	set_refresh_history(st);
	set_refresh_cfg(st);
	return 0;
}
