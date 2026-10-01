// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_ARGS_H
#define GRABIT_ARGS_H

#include <stdbool.h>

enum action {
	ACTION_NONE,
	ACTION_UPLOAD,
	ACTION_COPY,
	ACTION_OUTPUT,
	ACTION_OCR,
	ACTION_RECORD,
	ACTION_PIN,
	ACTION_PIN_GRAB,
	ACTION_PIN_RELEASE,
	ACTION_PIN_CLOSE_ALL,
	ACTION_REC_PAUSE,
	ACTION_TRAY,
};

struct args {
	enum action action;
	bool silent;
	bool edit;
	bool no_tray;
	bool no_upload;
	bool translate;
	bool show;
	bool no_copy;
	bool cursor;
	bool chunked;
	bool fullscreen;
	bool window;
	bool last_region;
	bool no_last;
	int delay_secs;
	bool delay_set;
	const char *file;
	const char *service;
	const char *filename_tpl;
	const char *out_path;
	const char *format;
	const char *translate_to;
	const char *fullscreen_target;
};

extern const char *const grabit_action_names[];

enum action args_default_action(const char *name);

void args_pre_scan(int argc, char **argv, bool *silent, bool *debug);
bool args_is_help_flag(const char *a);
bool args_is_version_flag(const char *a);
int args_parse(int argc, char **argv, struct args *out);

#endif
