// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ocr/ocr.h"

#include "log.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static bool path_looks_like_game(const char *path) {
	if (!path) return false;
	return strstr(path, "/games/") != NULL ||
		   strstr(path, "/Sauerbraten/") != NULL ||
		   strstr(path, "/tesseract-engine") != NULL;
}

int grabit_ocr_check(const char *bin) {
	if (!bin || !bin[0]) return -1;

	char resolved[4096];
	if (grabit_resolve_in_path(bin, resolved, sizeof resolved) != 0) return -1;
	if (path_looks_like_game(resolved)) {
		log_debug("ocr: rejecting %s (looks like Tesseract FPS game path)", resolved);
		return -1;
	}

	char *argv[] = {(char *)bin, (char *)"--version", NULL};
	struct grabit_buf buf = {0};
	int status = 0;
	if (grabit_spawn_capture(argv, true, 1024, &buf, NULL, &status) != 0) {
		grabit_buf_free(&buf);
		return -1;
	}
	const char *out = buf.data ? buf.data : "";
	int rc = -1;
	if (!WIFEXITED(status)) {
		log_debug("ocr: tesseract --version killed by signal %d", WTERMSIG(status));
		goto done;
	}
	int code = WEXITSTATUS(status);
	log_debug("ocr: %s --version exit=%d output=%.80s", bin, code, out);
	if (code != 0) goto done;
	if (!strstr(out, "tesseract") && !strstr(out, "Tesseract")) {
		log_error("ocr: `%s` --version did not look like Tesseract OCR", bin);
		goto done;
	}
	if (!strstr(out, "leptonica")) {
		log_error("ocr: `%s` is not Tesseract OCR (no leptonica in --version)", bin);
		log_error("  if this is the Tesseract FPS game, set ocr.tesseract to the OCR binary:");
		log_error("    grabit set ocr.tesseract /usr/bin/tesseract-ocr  # or the right path");
		goto done;
	}
	rc = 0;
done:
	grabit_buf_free(&buf);
	return rc;
}

int grabit_ocr_has_lang(const char *bin, const char *lang) {
	if (!bin || !bin[0] || !lang || !lang[0]) return -1;

	char *argv[] = {(char *)bin, (char *)"--list-langs", NULL};
	struct grabit_buf buf = {0};
	int status = 0;
	if (grabit_spawn_capture(argv, true, 1 << 16, &buf, NULL, &status) != 0) {
		grabit_buf_free(&buf);
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !buf.data) {
		grabit_buf_free(&buf);
		return -1;
	}

	int found = -1;
	size_t lang_len = strlen(lang);
	char *p_line = buf.data;
	while (p_line && *p_line) {
		char *nl = strchr(p_line, '\n');
		size_t len = nl ? (size_t)(nl - p_line) : strlen(p_line);
		while (len > 0 && (p_line[len - 1] == '\r' || p_line[len - 1] == ' ' ||
						   p_line[len - 1] == '\t'))
			len--;
		if (len == lang_len && strncmp(p_line, lang, lang_len) == 0) {
			found = 0;
			break;
		}
		if (!nl) break;
		p_line = nl + 1;
	}
	grabit_buf_free(&buf);
	return found;
}

char *grabit_ocr_run(const char *bin, const char *path, const char *lang) {
	if (!bin || !bin[0] || !path || !path[0]) return NULL;
	if (!lang || !lang[0]) lang = "eng";

	char *argv[] = {(char *)bin, (char *)path, (char *)"stdout",
					(char *)"-l", (char *)lang, NULL};
	struct grabit_buf buf = {0};
	int status = 0;
	if (grabit_spawn_capture(argv, false, 0, &buf, NULL, &status) != 0) {
		grabit_buf_free(&buf);
		return NULL;
	}
	if (!WIFEXITED(status)) {
		grabit_buf_free(&buf);
		log_error("ocr: tesseract killed by signal %d", WTERMSIG(status));
		return NULL;
	}
	int code = WEXITSTATUS(status);
	if (code != 0) {
		grabit_buf_free(&buf);
		if (code == 127) {
			log_error("ocr: tesseract not found in $PATH");
		} else {
			log_error("ocr: tesseract exited with code %d (see stderr above)", code);
		}
		return NULL;
	}

	if (!buf.data) {
		char *empty = malloc(1);
		if (empty) empty[0] = '\0';
		return empty;
	}

	buf.len = grabit_rstrip(buf.data, buf.len);
	return buf.data;
}
