// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_OCR_H
#define GRABIT_OCR_H

int grabit_ocr_check(const char *bin);
int grabit_ocr_has_lang(const char *bin, const char *lang);
char *grabit_ocr_run(const char *bin, const char *path, const char *lang);

struct grabit_translate_opts {
	const char *backend;
	const char *url;
	const char *api_key;
};

char *grabit_translate(const char *text, const char *target,
					   const struct grabit_translate_opts *opts);
char *grabit_translate_trans(const char *text, const char *target);
char *grabit_translate_libre(const char *text, const char *target,
							 const char *url, const char *api_key);
char *grabit_translate_deepl(const char *text, const char *target,
							 const char *url, const char *api_key);

#endif
