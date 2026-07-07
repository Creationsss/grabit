// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "capture/capture.h"
#include "capture/freeze.h"
#include "capture/save.h"
#include "clipboard/clipboard.h"
#include "config.h"
#include "log.h"
#include "mime.h"
#include "notify/notify.h"
#include "notify_test.h"
#include "ocr/ocr.h"
#include "paths.h"
#include "pin/pin.h"
#include "pin/preview.h"
#include "pin/text_card.h"
#include "plugin/dispatch.h"
#include "plugin/plugin.h"
#include "record/record.h"
#include "region/edit_persist.h"
#include "region/region.h"
#include "sound/sound.h"
#include "ui/config_ui.h"
#include "upload/upload.h"
#include "util.h"
#include "wl.h"

#ifndef GRABIT_VERSION
#define GRABIT_VERSION "0.5.0"
#endif

static char g_tmpfile_path[4096] = {0};

static void register_tmpfile(const char *path) {
	if (!path) return;
	size_t n = strlen(path);
	if (n >= sizeof g_tmpfile_path) n = sizeof g_tmpfile_path - 1;
	memcpy(g_tmpfile_path, path, n);
	g_tmpfile_path[n] = 0;
}

static void clear_tmpfile(void) {
	g_tmpfile_path[0] = 0;
}

static void on_signal(int sig) {
	if (g_tmpfile_path[0]) unlink(g_tmpfile_path);
	signal(sig, SIG_DFL);
	raise(sig);
}

static void install_signal_handlers(void) {
	grabit_install_signal_handler(SIGINT, on_signal);
	grabit_install_signal_handler(SIGTERM, on_signal);
	grabit_install_signal_handler(SIGHUP, on_signal);
}

static int print_version(void) {
	puts("grabit " GRABIT_VERSION);
	puts("capture backends: wlr-screencopy, ext-image-copy (KDE Plasma 6)");
	puts("Copyright (C) 2026 creations. AGPL-3.0-or-later.");
	return 0;
}

static int print_help(void) {
	fputs(
		"Usage: grabit [options]\n"
		"\n"
		"Capture & output:\n"
		"  -c, --copy        Copy screenshot to clipboard\n"
		"  -u, --upload      Upload screenshot to default service\n"
		"  --chunked         Upload to zipline in chunks (see services.zipline.chunked)\n"
		"  --<service>       Upload to a specific service\n"
		"                    (zipline|nest|fakecrime|ez|guns|pixelvault)\n"
		"  -o, --output, --save\n"
		"                    Capture and print path to stdout\n"
		"  -f <file>         Use <file> instead of taking a screenshot\n"
		"  --tesseract       Capture, OCR, copy text to clipboard\n"
		"  --translate[=<target-lang>]\n"
		"                    With --tesseract: pipe OCR through `trans` and copy the\n"
		"                    translation. <target-lang> is where to translate TO (source is\n"
		"                    auto-detected). Falls back to raw OCR if `trans` is missing or\n"
		"                    the translate call fails. Needs translate-shell installed.\n"
		"  --record          Toggle screen recording (re-run to stop)\n"
		"                    With --no-upload: skip auto-upload even if default_action=upload\n"
		"                    With --<service>: upload to that service after recording\n"
		"  --no-upload       Skip auto-upload after --record\n"
		"  --no-tray         Skip SNI tray during recording\n"
		"  --pin             Capture and pin to desktop (click-through; stack any number)\n"
		"  --show            With --tesseract: show the OCR/translate result on screen as\n"
		"                    a click-through text card instead of copying. Auto-dismisses\n"
		"                    after `text_card.dismiss_secs` (default 8s); a second --show\n"
		"                    replaces the previous card.\n"
		"  --grab            Make pinned screenshots clickable (click closes one; drag to move)\n"
		"                    Pair with a hold-bind, e.g. hyprland:\n"
		"                      bindrn = SUPER SHIFT, mouse:272, exec, grabit --grab\n"
		"                      bindrn = SUPER SHIFT, mouse:272, release, exec, grabit --release\n"
		"  --release         Restore pinned screenshots to click-through\n"
		"  --close-all       Dismiss every pinned screenshot\n"
		"  -e, --edit        Open the in-tree annotation editor before the action\n"
		"                    Set as default with: grabit set edit.default true\n"
		"  -F, --fullscreen  Capture a whole monitor instead of dragging a region.\n",
		stdout);
	fputs(
		"                    One monitor: grabs it directly. Multiple: hover a monitor\n"
		"                    and click to pick it (drag still works for a custom region).\n"
		"                    Skip the picker with --fullscreen=<n> (1-based),\n"
		"                    --fullscreen=<name> (e.g. --fullscreen=DP-1), or grab every\n"
		"                    monitor stitched together with --fullscreen=all. Works with\n"
		"                    -c/-u/-o/--pin/--tesseract/--record and pairs with -e.\n"
		"  --cursor          Include the mouse pointer even if capture.cursor=false\n"
		"  --silent, -q, --quiet  Suppress notifications and sound\n"
		"  -d, --debug       Enable debug logging to stderr\n"
		"  --filename <tpl>  Per-run filename template\n"
		"  --format <fmt>    Output format: png|jpeg|webp (default png)\n"
		"  --                End-of-options; following arg is treated as -f <file>\n"
		"\n"
		"Config (run `grabit set --help` for details):\n"
		"  config            Open the on-screen config editor (toggles/steppers)\n"
		"  test-notify       Fire a sample notification, sound, and preview\n"
		"  set <key> <val>   Write a config key (validated)\n"
		"  set <key>         Print example value for that key\n"
		"  set               List all available keys\n"
		"  get [<key>]       Print one config key, or every set key\n"
		"  unset <key>       Remove a config key\n"
		"\n"
		"Custom uploaders (ShareX .sxcu):\n"
		"  sxcu add <file>   Register a .sxcu uploader (use as --<name>)\n"
		"  sxcu list         Show registered uploaders (alias: ls)\n"
		"  sxcu show <name>  Print parsed fields\n"
		"  sxcu remove <name>  Remove an uploader (alias: rm)\n"
		"\n"
		"Plugins:\n"
		"  plugin install <git-url>  Clone, build, and install a plugin\n"
		"  plugin list               List installed plugins\n"
		"  plugin show <name>        Print parsed manifest\n"
		"  plugin update [<name>]    Update one plugin or all\n"
		"  plugin remove <name>      Uninstall a plugin\n"
		"  <name> ...                Run installed plugin `grabit-<name>`\n"
		"                            (auto-updates in background per manifest)\n"
		"  -p <name> ...             Run plugin, pin its last stdout line as a file\n"
		"\n"
		"Filename templates (--filename or `filename` config key):\n"
		"  %Y %m %d %H %M %S strftime fields\n"
		"  %s                unix timestamp\n"
		"  %r[N]             random alphanumeric, N chars (default 12)\n"
		"  %u                uuid v4\n"
		"  %w                active window class (hyprland)\n"
		"  %t                active window title (hyprland)\n"
		"  %o                output name where the capture happened\n"
		"\n"
		"First run seeds `default_action=copy` so a bare `grabit` opens the region selector\n"
		"and copies to the clipboard. Change with: grabit set default_action upload|copy|save|pin\n",
		stdout);
	fputs(
		"\n"
		"Examples:\n"
		"  grabit -c                       region screenshot to clipboard\n"
		"  grabit -u                       upload using `service` config\n"
		"  grabit --zipline                upload to a specific service\n"
		"  grabit -o > shot.txt            save and print path\n"
		"  grabit -e -u                    annotate, then upload\n"
		"  grabit --record                 start recording (re-run to stop)\n"
		"  grabit --pin                    pin a region to the desktop\n"
		"  grabit -f shot.png --zipline    upload an existing file\n"
		"  grabit --format jpeg -o         save as JPEG instead of PNG\n"
		"  grabit help <subcommand>        help for set/get/unset/sxcu/plugin\n"
		"\n"
		"Misc:\n"
		"  -V, --version     Print version and exit\n"
		"  -h, --help        Print this help and exit\n"
		"\n"
		"Environment:\n"
		"  GRABIT_DEBUG=1            Same as -d\n"
		"  GRABIT_<SERVICE>_AUTH     Auth token (overrides config). Service is one of\n"
		"                            ZIPLINE, NEST, FAKECRIME, EZ, GUNS, PIXELVAULT.\n"
		"                            Example: export GRABIT_ZIPLINE_AUTH=\"$(pass show grabit/zipline)\"\n"
		"  GRABIT_BIN                Set by plugin dispatch; absolute path to grabit\n"
		"  NO_COLOR                  Disable color in logs (https://no-color.org)\n",
		stdout);
	return 0;
}

static int read_int_cfg_clamp(struct config *cfg, const char *key,
							  int def, int lo, int hi) {
	const char *v = config_get(cfg, key);
	if (!v || !v[0]) return def;
	char *end = NULL;
	long n = strtol(v, &end, 10);
	if (!end || *end != '\0') return def;
	if (n < lo) return lo;
	if (n > hi) return hi;
	return (int)n;
}

static void maybe_show_preview(struct config *cfg, const char *image_path,
							   const char *caption, const char *click_open) {
	if (!image_path) return;
	const char *en = config_get(cfg, "preview.enabled");
	if (!en || strcmp(en, "true") != 0) return;

	char *png_path = paths_build_output(cfg, NULL, ".png", PATHS_DEST_TEMP);
	if (!png_path) return;

	int width = read_int_cfg_clamp(cfg, "preview.size", 300, 100, 800);
	const char *pos = config_get(cfg, "preview.position");
	if (!pos || !pos[0]) pos = "bottom-right";

	if (pin_preview_render_png(image_path, width, png_path) == 0) {
		struct pin_show_opts opts = {
			.dismiss_secs = read_int_cfg_clamp(cfg, "preview.dismiss_secs", 5, 0, 600),
			.position = pos,
			.output_name = config_get(cfg, "preview.output"),
			.hover_caption = caption,
			.click_open = click_open,
		};
		pin_spawn_show(cfg, png_path, &opts);
	}
	(void)unlink(png_path);
	free(png_path);
}

static int resolve_save_opts(const struct args *a, struct config *cfg,
							 struct grabit_save_opts *out) {
	*out = (struct grabit_save_opts){0};
	const char *fmt_name = a->format;
	if (!fmt_name) fmt_name = config_get(cfg, "format");
	if (!fmt_name) fmt_name = "png";
	if (grabit_format_from_name(fmt_name, &out->format) != 0) {
		log_error("unknown format `%s` (expected png|jpeg|webp)", fmt_name);
		return -1;
	}
	out->jpeg_quality = read_int_cfg_clamp(cfg, "jpeg.quality", 90, 1, 100);
	out->webp_quality = read_int_cfg_clamp(cfg, "webp.quality", 85, 0, 100);
	const char *wl = config_get(cfg, "webp.lossless");
	out->webp_lossless = wl && strcmp(wl, "true") == 0;
	return 0;
}

static char *build_capture_path(const struct args *a, struct config *cfg,
								enum action eff, bool *is_temp,
								const struct grabit_save_opts *opts) {
	bool save;
	if (eff == ACTION_OUTPUT) {
		save = true;
	} else if (eff == ACTION_OCR) {
		save = false;
	} else {
		save = config_also_save(cfg);
	}
	*is_temp = !save;
	enum paths_dest dest = save ? PATHS_DEST_PICTURES : PATHS_DEST_TEMP;
	const char *ext = grabit_format_extension(opts->format);
	return paths_build_output(cfg, a->filename_tpl, ext, dest);
}

static char *capture_to_file(const struct args *a, struct config *cfg,
							 enum action eff, bool *is_temp,
							 struct rect *out_rect) {
	*is_temp = false;
	struct grabit_save_opts opts;
	if (resolve_save_opts(a, cfg, &opts) != 0) return NULL;

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) {
		notify_send(&(struct notify_opts){
			.summary = "grabit",
			.body = "could not connect to wayland compositor",
			.force = true,
		});
		return NULL;
	}

	struct rect fs_rect;
	const struct rect *forced = NULL;
	if (a->fullscreen) {
		int plan = grabit_wl_fullscreen_plan(&s, a->fullscreen_target, &fs_rect);
		if (plan < 0) {
			grabit_wl_finish(&s);
			notify_send(&(struct notify_opts){
				.summary = "grabit: fullscreen failed",
				.body = "no matching monitor; see terminal for details",
				.force = true,
			});
			return NULL;
		}
		if (plan == 0) forced = &fs_rect;
	}

	char *path = build_capture_path(a, cfg, eff, is_temp, &opts);
	if (!path) {
		grabit_wl_finish(&s);
		notify_send(&(struct notify_opts){
			.summary = "grabit: capture failed",
			.body = "could not build output path; see terminal for details",
			.force = true,
		});
		return NULL;
	}

	if (*is_temp) register_tmpfile(path);

	struct rect *mon_rects = NULL;
	size_t n_mon = 0;
	if (a->fullscreen && !forced)
		grabit_wl_monitor_rects(&s, &mon_rects, &n_mon);

	uint32_t edit_color = edit_color_from_str(config_get(cfg, "edit.color"));
	int32_t edit_width = edit_width_from_str(config_get(cfg, "edit.width"));
	int32_t edit_tool = edit_tool_from_str(config_get(cfg, "edit.tool"));
	bool edit_dirty = false;

	const char *cursor_cfg = config_get(cfg, "capture.cursor");
	bool cursor = a->cursor || !cursor_cfg || strcmp(cursor_cfg, "false") != 0;
	int rc = grabit_freeze_capture(&s, cfg, path, &opts, out_rect, a->edit, cursor,
								   a->edit ? &edit_color : NULL,
								   a->edit ? &edit_width : NULL,
								   a->edit ? &edit_tool : NULL,
								   a->edit ? &edit_dirty : NULL, forced, mon_rects, n_mon);
	grabit_wl_finish(&s);
	free(mon_rects);

	if (a->edit && edit_dirty) persist_edit_choices(cfg, edit_color, edit_width, edit_tool);

	if (rc != 0) {
		unlink(path);
		clear_tmpfile();
		free(path);
		if (rc != GRABIT_CAPTURE_CANCELLED) {
			notify_send(&(struct notify_opts){
				.summary = "grabit: capture failed",
				.body = "see terminal for details",
				.force = true,
			});
		}
		return NULL;
	}

	log_debug("captured to %s", path);
	return path;
}

static char *acquire_source(const struct args *a, struct config *cfg,
							enum action eff, bool *is_temp,
							struct rect *out_rect) {
	if (a->file) {
		char *path = strdup(a->file);
		if (!path) log_error("out of memory");
		return path;
	}
	return capture_to_file(a, cfg, eff, is_temp, out_rect);
}

static void release_source(char *path, bool is_temp) {
	if (is_temp) {
		unlink(path);
		clear_tmpfile();
	}
	free(path);
}

static int run_upload(struct config *cfg, const struct args *a) {
	const char *service = NULL;
	if (upload_preflight(cfg, a, &service) != 0) return 1;

	bool is_temp = false;
	char *path = acquire_source(a, cfg, ACTION_UPLOAD, &is_temp, NULL);
	if (!path) return 1;

	struct upload_result r = {0};
	int rc = upload_perform(service, path, cfg, a->chunked, &r);

	if (rc == 0) {
		clipboard_set_text(r.url);
		char *m = mime_for_file(path);
		const char *summary = mime_is_video(m) ? "Video uploaded" : "Uploaded";
		struct notify_opts opts = {
			.summary = summary,
			.body = "link copied to clipboard",
			.icon_path = mime_is_image(m) ? path : NULL,
		};
		notify_send(&opts);
		grabit_sound_play(cfg);
		if (mime_is_image(m)) {
			maybe_show_preview(cfg, path, "Uploaded", r.url);
		}
		free(m);
		puts(r.url);
		fflush(stdout);
	} else {
		char body[256];
		upload_friendly_error(&r, body, sizeof body);
		if (is_temp) {
			log_info("upload failed; capture kept at %s", path);
			log_info("retry with: grabit -f %s --%s", path, service);
			is_temp = false;
			clear_tmpfile();
		}
		notify_send(&(struct notify_opts){
			.summary = "Upload failed",
			.body = body,
			.force = true,
		});
	}

	upload_result_free(&r);
	release_source(path, is_temp);
	return rc == 0 ? 0 : 1;
}

static int run_copy(struct config *cfg, const struct args *a) {
	bool is_temp = false;
	char *path = acquire_source(a, cfg, ACTION_COPY, &is_temp, NULL);
	if (!path) return 1;

	int rc = clipboard_set_image_file(path);

	const char *base = grabit_basename(path);
	if (!base || !base[0]) base = path;
	if (rc == 0) {
		notify_send(&(struct notify_opts){
			.summary = "Copied to clipboard",
			.body = base,
			.icon_path = path,
		});
		grabit_sound_play(cfg);
		maybe_show_preview(cfg, path, "Copied", NULL);
	} else {
		notify_send(&(struct notify_opts){
			.summary = "Clipboard write failed",
			.body = "wayland clipboard rejected the image; see terminal for details",
			.force = true,
		});
	}

	release_source(path, is_temp);
	return rc == 0 ? 0 : 1;
}

static int run_output(struct config *cfg, const struct args *a) {
	if (a->file) {
		puts(a->file);
		return 0;
	}
	bool is_temp = false;
	char *path = capture_to_file(a, cfg, ACTION_OUTPUT, &is_temp, NULL);
	if (!path) return 1;

	puts(path);
	if (isatty(STDOUT_FILENO)) {
		notify_send(&(struct notify_opts){
			.summary = "Saved",
			.body = grabit_basename(path),
			.icon_path = path,
		});
		grabit_sound_play(cfg);
	}
	char dir[4096];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash && slash != dir) *slash = '\0';
	maybe_show_preview(cfg, path, grabit_basename(path), dir);
	free(path);
	return 0;
}

static int run_ocr(struct config *cfg, const struct args *a) {
	const char *bin = config_get(cfg, "ocr.tesseract");
	if (bin && bin[0] && grabit_ocr_check(bin) != 0) {
		log_error("ocr: configured ocr.tesseract `%s` not found; "
				  "unset with: grabit unset ocr.tesseract",
				  bin);
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "configured tesseract not found; see terminal for details",
		});
		return 1;
	}
	if (!bin || !bin[0]) {
		static const char *const CANDIDATES[] = {"tesseract-ocr", "tesseract", NULL};
		for (size_t i = 0; CANDIDATES[i]; i++) {
			if (grabit_ocr_check(CANDIDATES[i]) == 0) {
				bin = CANDIDATES[i];
				break;
			}
		}
	}
	if (!bin) {
		log_error("ocr: tesseract not found in $PATH (install tesseract)");
		log_error("  also need the english training data: tesseract-data-eng (arch), "
				  "tesseract-ocr-eng (debian/ubuntu)");
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "install tesseract + the english training data",
		});
		return 1;
	}
	if (grabit_ocr_has_lang(bin, "eng") != 0) {
		log_error("ocr: tesseract is installed but the english language data isn't");
		log_error("  install: tesseract-data-eng (arch), tesseract-ocr-eng (debian/ubuntu)");
		log_error("  or set TESSDATA_PREFIX to the dir containing eng.traineddata");
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "tesseract eng.traineddata missing; install the language pack",
			.force = true,
		});
		return 1;
	}

	bool is_temp = false;
	char *path = acquire_source(a, cfg, ACTION_OCR, &is_temp, NULL);
	if (!path) return 1;

	char *text = grabit_ocr_run(bin, path);

	release_source(path, is_temp);

	if (!text) {
		notify_send(&(struct notify_opts){
			.summary = "OCR failed",
			.body = "tesseract returned no output; check that eng.traineddata is installed",
			.force = true,
		});
		return 1;
	}
	if (!text[0]) {
		free(text);
		log_info("ocr: no text found in selection");
		notify_send(&(struct notify_opts){
			.summary = "ocr: no text found",
		});
		return 1;
	}

	bool translated = false;
	if (a->translate) {
		const char *target = a->translate_to;
		if (!target || !target[0]) target = config_get(cfg, "translate.target");
		if (!target || !target[0]) target = "en";
		if (!grabit_in_path("trans")) {
			log_warn("translate: `trans` not in $PATH; copying raw OCR text");
			notify_send(&(struct notify_opts){
				.summary = "Translate skipped",
				.body = "translate-shell not installed; raw OCR copied",
				.force = true,
			});
		} else {
			char *translated_text = grabit_translate(text, target);
			if (!translated_text) {
				log_warn("translate: failed; copying raw OCR text");
				notify_send(&(struct notify_opts){
					.summary = "Translate failed",
					.body = "see terminal for details; raw OCR copied",
					.force = true,
				});
			} else {
				free(text);
				text = translated_text;
				translated = true;
			}
		}
	}

	if (a->show) {
		char *png_path = paths_build_output(cfg, NULL, ".png", PATHS_DEST_TEMP);
		if (!png_path || pin_text_card_render_png(text, png_path) != 0) {
			log_error("ocr: text card render failed");
			notify_send(&(struct notify_opts){
				.summary = "Show failed",
				.body = "could not render OCR text card; see terminal for details",
				.force = true,
			});
			free(png_path);
			free(text);
			return 1;
		}
		struct pin_show_opts opts = {
			.dismiss_secs = read_int_cfg_clamp(cfg, "text_card.dismiss_secs", 8, 0, 600),
			.position = config_get(cfg, "text_card.position"),
			.output_name = config_get(cfg, "text_card.output"),
		};
		int rc = pin_spawn_show(cfg, png_path, &opts);
		(void)unlink(png_path);
		free(png_path);
		if (rc != 0) {
			free(text);
			return 1;
		}
		log_info("ocr: %zu chars shown on screen%s",
				 strlen(text), translated ? " (translated)" : "");
		grabit_sound_play(cfg);
		free(text);
		return 0;
	}

	if (clipboard_set_text(text) != 0) {
		log_error("ocr: clipboard write failed");
		notify_send(&(struct notify_opts){
			.summary = "Clipboard write failed",
			.body = "OCR text not copied; see terminal for details",
			.force = true,
		});
		free(text);
		return 1;
	}

	size_t tlen = strlen(text);
	enum { PREVIEW_MAX = 800 };
	char preview[PREVIEW_MAX + 8];
	char flat[PREVIEW_MAX + 1];
	size_t fi = 0;
	bool prev_space = false;
	bool overflowed = false;
	for (size_t i = 0; text[i]; i++) {
		unsigned char ch = (unsigned char)text[i];
		bool is_ws = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
		if (fi >= sizeof flat - 1) {
			overflowed = true;
			break;
		}
		if (is_ws) {
			if (!prev_space && fi > 0) flat[fi++] = ' ';
			prev_space = true;
		} else {
			flat[fi++] = (char)ch;
			prev_space = false;
		}
	}
	while (fi > 0 && ((unsigned char)flat[fi - 1] & 0xC0) == 0x80)
		fi--;
	while (fi > 0 && flat[fi - 1] == ' ')
		fi--;
	flat[fi] = '\0';
	bool truncated = overflowed || tlen > fi + 16;
	if (truncated) {
		snprintf(preview, sizeof preview, "%s…", flat);
	} else {
		snprintf(preview, sizeof preview, "%s", flat);
	}

	log_info("ocr: %zu chars copied to clipboard%s", tlen,
			 translated ? " (translated)" : "");
	notify_send(&(struct notify_opts){
		.summary = translated ? "OCR + Translate" : "OCR Complete",
		.body = preview,
	});
	grabit_sound_play(cfg);

	free(text);
	return 0;
}

static int run_record(struct config *cfg, const struct args *a) {
	return record_toggle(cfg, a);
}

static int run_pin(struct config *cfg, const struct args *a) {
	bool is_temp = false;
	struct rect r = {0};
	char *path = acquire_source(a, cfg, ACTION_PIN, &is_temp, &r);
	if (!path) return 1;
	bool have_rect = (r.w > 0 && r.h > 0);

	int rc = pin_spawn(cfg, path, have_rect ? &r : NULL);

	if (rc == 0) grabit_sound_play(cfg);

	release_source(path, is_temp);
	return rc == 0 ? 0 : 1;
}

static int run(const struct args *a) {
	struct config cfg;
	if (config_load(&cfg) != 0) return 1;
	notify_init(&cfg, a->silent);

	const char *backend_pref = config_get(&cfg, "capture.backend");
	if (backend_pref && backend_pref[0] && !getenv("GRABIT_CAPTURE_BACKEND"))
		setenv("GRABIT_CAPTURE_BACKEND", backend_pref, 1);

	enum action eff = a->action;
	if (eff == ACTION_NONE) {
		const char *def = config_get(&cfg, "default_action");
		if (def && strcmp(def, "upload") == 0)
			eff = ACTION_UPLOAD;
		else if (def && strcmp(def, "copy") == 0)
			eff = ACTION_COPY;
		else if (def && strcmp(def, "save") == 0)
			eff = ACTION_OUTPUT;
		else if (def && strcmp(def, "pin") == 0)
			eff = ACTION_PIN;
	}

	struct args eff_a = *a;
	if (!eff_a.edit && !a->file &&
		(eff == ACTION_UPLOAD || eff == ACTION_COPY ||
		 eff == ACTION_OUTPUT || eff == ACTION_PIN)) {
		const char *v = config_get(&cfg, "edit.default");
		if (v && strcmp(v, "true") == 0) eff_a.edit = true;
	}
	a = &eff_a;

	int rc;
	switch (eff) {
	case ACTION_UPLOAD:
		rc = run_upload(&cfg, a);
		break;
	case ACTION_COPY:
		rc = run_copy(&cfg, a);
		break;
	case ACTION_OUTPUT:
		rc = run_output(&cfg, a);
		break;
	case ACTION_OCR:
		rc = run_ocr(&cfg, a);
		break;
	case ACTION_RECORD:
		rc = run_record(&cfg, a);
		break;
	case ACTION_PIN:
		rc = run_pin(&cfg, a);
		break;
	case ACTION_PIN_GRAB:
		rc = pin_grab();
		break;
	case ACTION_PIN_RELEASE:
		rc = pin_release();
		break;
	case ACTION_PIN_CLOSE_ALL:
		rc = pin_close_all();
		break;
	default:
		log_error("no action specified; try -u, -c, -o, --pin, --record, or --tesseract");
		log_info("(or set a default with: grabit set default_action upload)");
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "no action; run `grabit set default_action upload|copy|save|pin`",
		});
		rc = 1;
		break;
	}

	config_free(&cfg);
	return rc;
}

static bool is_value_flag(const char *s) {
	return strcmp(s, "-o") == 0 || strcmp(s, "--output") == 0 ||
		   strcmp(s, "--save") == 0 || strcmp(s, "-f") == 0 ||
		   strcmp(s, "--filename") == 0;
}

static bool plugin_argv_has_input(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (is_value_flag(argv[i])) i++;
			continue;
		}
		return true;
	}
	return false;
}

static int try_dispatch_plugin(const char *name, int argc, char **argv) {
	if (!plugin_name_is_valid(name)) return -1;
	char path[1024];
	if (plugin_resolve(name, path, sizeof path) != 0) return -1;

	plugin_maybe_auto_update(name);
	plugin_dispatch_set_env(name);

	bool force_capture = false;
	bool no_capture = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--capture") == 0)
			force_capture = true;
		else if (strcmp(argv[i], "--no-capture") == 0)
			no_capture = true;
	}

	bool manifest_auto = false;
	char manifest_path[1024];
	int n = snprintf(manifest_path, sizeof manifest_path, "%s/%s/manifest.toml",
					 plugin_dir_path(), name);
	if (n > 0 && (size_t)n < sizeof manifest_path) {
		struct plugin_manifest m;
		if (plugin_manifest_parse_file(manifest_path, &m) == 0) {
			manifest_auto = m.capture_auto;
			plugin_manifest_free(&m);
		}
	}

	bool want_capture = !no_capture &&
						(force_capture ||
						 (manifest_auto && !plugin_argv_has_input(argc, argv)));

	char *captured = NULL;
	struct config cap_cfg;
	bool cap_cfg_loaded = false;
	bool cap_is_temp = false;
	if (want_capture) {
		if (config_load(&cap_cfg) != 0) {
			log_error("plugin: --capture: config_load failed");
			exit(1);
		}
		cap_cfg_loaded = true;
		struct args ca = {0};
		captured = capture_to_file(&ca, &cap_cfg, ACTION_OUTPUT, &cap_is_temp, NULL);
		if (!captured) {
			config_free(&cap_cfg);
			exit(1);
		}
		log_debug("plugin: captured %s for %s", captured, name);
	}

	int extra = captured ? 1 : 0;
	char **new_argv = calloc((size_t)argc + 1 + extra, sizeof *new_argv);
	if (!new_argv) {
		if (cap_cfg_loaded) config_free(&cap_cfg);
		free(captured);
		return -1;
	}
	int o = 0;
	new_argv[o++] = path;
	if (captured) new_argv[o++] = captured;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--capture") == 0) continue;
		if (strcmp(argv[i], "--no-capture") == 0) continue;
		new_argv[o++] = argv[i];
	}

	execv(path, new_argv);
	int err = errno;
	free(new_argv);
	if (cap_cfg_loaded) config_free(&cap_cfg);
	free(captured);
	log_error("plugin: exec %s: %s", path, strerror(err));
	return 1;
}

int main(int argc, char **argv) {
	bool pre_silent = false, pre_debug = false;
	args_pre_scan(argc, argv, &pre_silent, &pre_debug);
	log_init(pre_silent, pre_debug);
	install_signal_handlers();

	if (argc >= 2) {
		const char *first = argv[1];
		if (strcmp(first, "--version") == 0 || strcmp(first, "-V") == 0) return print_version();
		if (strcmp(first, "--help") == 0 || strcmp(first, "-h") == 0) return print_help();
		if (strcmp(first, "help") == 0) {
			if (argc < 3) return print_help();
			const char *sub = argv[2];
			static char *help_argv[] = {(char *)"--help", NULL};
			if (strcmp(sub, "set") == 0) return cmd_set(1, help_argv);
			if (strcmp(sub, "get") == 0) return cmd_get(1, help_argv);
			if (strcmp(sub, "unset") == 0) return cmd_unset(1, help_argv);
			if (strcmp(sub, "sxcu") == 0) return cmd_sxcu(1, help_argv);
			if (strcmp(sub, "plugin") == 0) return cmd_plugin(1, help_argv);
			log_error("no help topic for `%s`", sub);
			return 1;
		}
		if (strcmp(first, "config") == 0) return grabit_config_ui();
		if (strcmp(first, "test-notify") == 0) {
			struct config cfg;
			if (config_load(&cfg) != 0) return 1;
			int rc = grabit_notify_test(&cfg);
			config_free(&cfg);
			return rc == 0 ? 0 : 1;
		}
		if (strcmp(first, "set") == 0) return cmd_set(argc - 2, argv + 2);
		if (strcmp(first, "get") == 0) return cmd_get(argc - 2, argv + 2);
		if (strcmp(first, "unset") == 0) return cmd_unset(argc - 2, argv + 2);
		if (strcmp(first, "sxcu") == 0) return cmd_sxcu(argc - 2, argv + 2);
		if (strcmp(first, "plugin") == 0) return cmd_plugin(argc - 2, argv + 2);
		if (strcmp(first, "-p") == 0) {
			if (argc < 3) {
				log_error("usage: grabit -p <plugin> [args]");
				return 1;
			}
			return plugin_dispatch_pin(argv[2], argc - 2, argv + 2);
		}
		if (first[0] != '-') {
			int prc = try_dispatch_plugin(first, argc - 1, argv + 1);
			if (prc >= 0) return prc;
		}
	}

	struct args a;
	if (args_parse(argc, argv, &a) != 0) return 2;
	return run(&a);
}
