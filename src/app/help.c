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

int gapp_print_version(void) {
	puts("grabit " GRABIT_VERSION);
	puts("capture backends: wlr-screencopy, ext-image-copy, kwin-screenshot (KDE)");
#ifdef HAVE_PIPEWIRE
	puts("recording sources: wlr/ext screencopy, kwin screencast, mutter screencast (pipewire)");
#else
	puts("recording sources: wlr/ext screencopy (built without pipewire)");
#endif
	puts("Copyright (C) 2026 creations. AGPL-3.0-or-later.");
	return 0;
}

int gapp_print_help(void) {
	fputs(
		"Usage: grabit [options]\n"
		"       grabit <set|get|unset|sxcu|plugin> ...\n"
		"       grabit help <topic>\n"
		"\n"
		"Actions:\n"
		"  -c, --copy          copy to clipboard\n"
		"  -u, --upload        upload to the default service\n"
		"  --<service>         upload to zipline|nest|fakecrime|ez|guns|pixelvault|<sxcu>\n"
		"  -o, --output        capture, save, print the path\n"
		"  --record            toggle screen recording\n"
		"  --pin               pin a capture to the desktop\n"
		"  --grab, --release, --close-all   manage existing pins\n"
		"  --tray              toggle the persistent tray icon (background process)\n"
		"  --tesseract         OCR a region to the clipboard\n"
		"  --translate[=lang]  with --tesseract: copy the translation instead\n"
		"\n"
		"Modifiers:\n"
		"  -e, --edit          annotate before the action\n"
		"  -F, --fullscreen[=<n|name|all>]  capture a whole monitor\n"
		"  -w, --window        capture the active window\n"
		"  -L, --last          reuse the last region instead of selecting one\n"
		"  --no-last           force the selector even if region.repeat_last is set\n"
		"  --delay <secs>      wait before capturing (menus, tooltips)\n"
		"  -f <file>           use an existing file instead of capturing\n"
		"  --format <fmt>      png|jpeg|webp\n"
		"  --filename <tpl>    per-run filename template\n"
		"  --cursor            include the pointer this run\n"
		"  --chunked           chunked zipline upload\n"
		"  --show              with --tesseract: also show the result on screen\n"
		"  --no-copy           with --tesseract --show: show only, do not copy\n"
		"  --no-upload         with --record: skip the auto-upload\n"
		"  --no-tray           with --record: no tray icon\n"
		"  --silent, -q        no sound, no info logs, only failure notifications\n"
		"  -d, --debug         debug logging to stderr\n"
		"  --                  next argument is treated as -f <file>\n"
		"  -V, --version       print version\n"
		"  -h, --help          print this help\n"
		"\n"
		"Subcommands:\n"
		"  set, get, unset     configuration\n"
		"  sxcu                ShareX uploaders\n"
		"  plugin              plugins\n"
		"  <name> ...          run an installed plugin (-p pins its output)\n"
		"\n"
		"Topics: grabit help <set|get|unset|sxcu|plugin|filename|env|examples|ocr>\n"
		"Full documentation: man grabit\n",
		stdout);
	return 0;
}

int gapp_print_help_topics(void) {
	fputs(
		"Usage: grabit help <topic>\n"
		"\n"
		"  set, get, unset   configuration commands\n"
		"  sxcu              ShareX (.sxcu) uploaders\n"
		"  plugin            plugin management\n"
		"  filename          filename template tokens\n"
		"  env               environment variables\n"
		"  ocr               ocr and translation\n"
		"  examples          common invocations\n"
		"\n"
		"grabit --help lists every flag; man grabit is the full reference.\n",
		stdout);
	return 0;
}

int gapp_print_help_filename(void) {
	fputs(
		"Filename templates (--filename, or the `filename` config key):\n"
		"\n"
		"  %Y %m %d %H %M %S   strftime fields\n"
		"  %s                  unix timestamp\n"
		"  %r[N]               random alphanumeric, N chars (default 12)\n"
		"  %u                  uuid v4\n"
		"  %w                  active window class / app id\n"
		"  %t                  active window title\n"
		"  %%                  a literal percent sign\n"
		"\n"
		"`filename_preset` (date|random|uuid|timestamp) sets a ready-made template.\n",
		stdout);
	return 0;
}

int gapp_print_help_ocr(void) {
	fputs(
		"Usage: grabit --tesseract [--translate[=<lang>]] [--show] [--no-copy]\n"
		"\n"
		"  --tesseract         select a region, OCR it, copy the text\n"
		"  --translate[=<to>]  translate the text first (default en)\n"
		"  --show              also show the text on screen as a transient card\n"
		"  --no-copy           with --show, do not copy (show only)\n"
		"\n"
		"Config:\n"
		"\n"
		"  ocr.tesseract       path to the tesseract binary (default: found on $PATH)\n"
		"  ocr.lang            language passed to tesseract (default eng)\n"
		"  translate.backend   trans|libretranslate|deepl (default trans)\n"
		"  translate.target    default target language\n"
		"  translate.url       server url for libretranslate/deepl\n"
		"  translate.api_key   api key; GRABIT_TRANSLATE_KEY overrides it\n"
		"\n"
		"Needs tesseract on $PATH plus the training data for the language you OCR.\n"
		"If translation fails the raw OCR text is copied instead.\n",
		stdout);
	return 0;
}

int gapp_print_help_env(void) {
	fputs(
		"Environment:\n"
		"\n"
		"  GRABIT_DEBUG=1            same as -d\n"
		"  GRABIT_<SERVICE>_AUTH     auth token, overrides config. <SERVICE> is one of\n"
		"                            ZIPLINE, NEST, FAKECRIME, EZ, GUNS, PIXELVAULT.\n"
		"  GRABIT_TRANSLATE_KEY      api key for the deepl/libretranslate backends\n"
		"  GRABIT_LOG_FILE=0         disable the log file (stderr only)\n"
		"  GRABIT_CAPTURE_BACKEND    force a capture backend (wlr|ext|kwin)\n"
		"  GRABIT_CLIPBOARD_BACKEND  force a clipboard backend (ext|wlr)\n"
		"  GRABIT_BIN                set by plugin dispatch; absolute path to grabit\n"
		"  NO_COLOR                  disable color in logs\n"
		"\n"
		"XDG_CONFIG_HOME and XDG_STATE_HOME choose where config.toml and state.toml\n"
		"live. man grabit lists the rest.\n",
		stdout);
	return 0;
}

int gapp_print_help_examples(void) {
	fputs(
		"Examples:\n"
		"\n"
		"  grabit -c                     region screenshot to the clipboard\n"
		"  grabit -u                     upload using the `service` config key\n"
		"  grabit -o > shot.txt          save and print the path\n"
		"  grabit -e -u                  annotate, then upload\n"
		"  grabit -F -o                  capture a whole monitor\n"
		"  grabit --record               start recording (run again to stop)\n"
		"  grabit -f shot.png --zipline  upload an existing file\n",
		stdout);
	return 0;
}
