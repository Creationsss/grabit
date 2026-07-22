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
#include "config/config.h"
#include "log.h"
#include "mime.h"
#include "notify/notify.h"
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
#include "upload/upload.h"
#include "util/util.h"
#include "wl/wl.h"

#ifndef GRABIT_VERSION
#define GRABIT_VERSION "0.0.0"
#endif
#include "app/app.h"

int gapp_run_ocr(struct config *cfg, const struct args *a) {
	const char *lang = config_get(cfg, "ocr.lang");
	if (!lang || !lang[0]) lang = "eng";

	const char *bin = config_get(cfg, "ocr.tesseract");
	if (bin && bin[0] && grabit_ocr_check(bin) != 0) {
		log_error("ocr: configured ocr.tesseract `%s` not found; "
				  "unset with: grabit unset ocr.tesseract",
				  bin);
		notify_send(&(struct notify_opts){
			.summary = "grabit: tesseract not found",
			.body = "configured tesseract not found",
			.log_hint = true,
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
		log_error("  also need the `%s` training data: tesseract-data-%s (arch), "
				  "tesseract-ocr-%s (debian/ubuntu)",
				  lang, lang, lang);
		notify_send(&(struct notify_opts){
			.summary = "grabit: tesseract not installed",
			.body = "install tesseract + the matching training data",
		});
		return 1;
	}
	if (grabit_ocr_has_lang(bin, lang) != 0) {
		log_error("ocr: tesseract is installed but the `%s` language data isn't", lang);
		log_error("  install: tesseract-data-%s (arch), tesseract-ocr-%s (debian/ubuntu)",
				  lang, lang);
		log_error("  or set TESSDATA_PREFIX to the dir containing %s.traineddata", lang);
		log_error("  list what's available with: %s --list-langs", bin);
		notify_send(&(struct notify_opts){
			.summary = "grabit: language data missing",
			.body = "tesseract language data missing; install the language pack",
			.force = true,
		});
		return 1;
	}

	bool is_temp = false;
	char *path = gapp_acquire_source(a, cfg, ACTION_OCR, &is_temp, NULL);
	if (!path) return 1;

	char *text = grabit_ocr_run(bin, path, lang);

	gapp_release_source(path, is_temp);

	if (!text) {
		notify_send(&(struct notify_opts){
			.summary = "OCR failed",
			.body = "tesseract returned no output; check the language data is installed",
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
		const char *backend = config_get(cfg, "translate.backend");
		if (!backend || !backend[0]) backend = "trans";
		const char *api_key = getenv("GRABIT_TRANSLATE_KEY");
		if (!api_key || !api_key[0]) api_key = config_get(cfg, "translate.api_key");
		struct grabit_translate_opts topts = {
			.backend = backend,
			.url = config_get(cfg, "translate.url"),
			.api_key = api_key,
		};
		if (strcmp(backend, "trans") == 0 && !grabit_in_path("trans")) {
			log_warn("translate: `trans` not in $PATH; copying raw OCR text");
			log_warn("  install translate-shell, or point grabit at a libretranslate server:");
			log_warn("    grabit set translate.backend libretranslate");
			log_warn("    grabit set translate.url http://localhost:5000");
			notify_send(&(struct notify_opts){
				.summary = "Translate skipped",
				.body = "translate-shell not installed; raw OCR copied",
				.force = true,
			});
		} else {
			char *translated_text = grabit_translate(text, target, &topts);
			if (!translated_text) {
				log_warn("translate: failed; copying raw OCR text");
				notify_send(&(struct notify_opts){
					.summary = "Translate failed",
					.body = "raw OCR copied",
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
				.body = "could not render OCR text card",
				.force = true,
			});
			free(png_path);
			free(text);
			return 1;
		}
		struct pin_show_opts opts = {
			.dismiss_secs = gapp_read_int_cfg_clamp(cfg, "text_card.dismiss_secs", 8, 0, 600),
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
			.body = "OCR text not copied",
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
