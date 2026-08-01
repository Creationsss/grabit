// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/apptray.h"

#include "config/config.h"

#include "log.h"
#include "tray/menu.h"
#include "tray/sni.h"
#include "util/util.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_app_stop = 0;

static void app_stop_signal(int sig) {
	(void)sig;
	g_app_stop = 1;
}

static int g_tray_lock_fd = -1;

static const char *tray_pid_path(void) {
	static char path[1024];
	if (grabit_runtime_file("grabit-tray.pid", path, sizeof path) != 0) {
		log_error("tray: no usable runtime dir for the tray pid file");
		return NULL;
	}
	return path;
}

static int stop_running_tray(void) {
	const char *p = tray_pid_path();
	if (!p) return -1;
	pid_t pid = grabit_lock_owner(p);
	if (pid <= 0) return -1;
	log_info("tray: stopping (pid %d)", (int)pid);
	if (kill(pid, SIGTERM) != 0) {
		log_error("tray: kill(%d): %s", (int)pid, strerror(errno));
		return -1;
	}
	return 0;
}

static int write_tray_pid(void) {
	const char *p = tray_pid_path();
	if (!p) return -1;
	g_tray_lock_fd = grabit_lock_acquire(p);
	return g_tray_lock_fd < 0 ? -1 : 0;
}

static void unlink_tray_pid(void) {
	const char *p = tray_pid_path();
	if (p) unlink(p);
	if (g_tray_lock_fd >= 0) close(g_tray_lock_fd);
	g_tray_lock_fd = -1;
}

static void spawn_grabit(const char *const *args) {
	pid_t pid = fork();
	if (pid < 0) {
		log_warn("tray: fork: %s", strerror(errno));
		return;
	}
	if (pid == 0) {
		grabit_double_fork_detach();
		char self[1024];
		bool have_self = grabit_self_exe(self, sizeof self) == 0;
		char *argv[8];
		size_t i = 0;
		argv[i++] = have_self ? self : (char *)"grabit";
		for (size_t j = 0; args[j] && i + 1 < sizeof argv / sizeof argv[0]; j++)
			argv[i++] = (char *)args[j];
		argv[i] = NULL;
		execv(argv[0], argv);
		execvp("grabit", argv);
		_exit(127);
	}
	(void)grabit_waitpid_intr(pid, NULL);
}

static const char *const ARGS_SHOT_REGION[] = {NULL};
static const char *const ARGS_SHOT_MONITOR[] = {"-F", NULL};
static const char *const ARGS_SHOT_COPY[] = {"-c", NULL};
static const char *const ARGS_SHOT_UPLOAD[] = {"-u", NULL};
static const char *const ARGS_SHOT_SAVE[] = {"-o", NULL};
static const char *const ARGS_SHOT_EDIT[] = {"-e", NULL};
static const char *const ARGS_REC_REGION[] = {"--record", NULL};
static const char *const ARGS_REC_MONITOR[] = {"--record", "-F", NULL};
static const char *const ARGS_OCR_COPY[] = {"--tesseract", NULL};
static const char *const ARGS_OCR_SHOW[] = {"--tesseract", "--show", NULL};
static const char *const ARGS_OCR_TRANSLATE[] = {"--tesseract", "--translate", NULL};
static const char *const ARGS_PIN_NEW[] = {"--pin", NULL};
static const char *const ARGS_PIN_GRAB[] = {"--grab", NULL};
static const char *const ARGS_PIN_RELEASE[] = {"--release", NULL};
static const char *const ARGS_PIN_CLOSE[] = {"--close-all", NULL};

static void click_spawn(const struct tray_menu_item *it) {
	spawn_grabit(it->user);
}

static void click_quit(const struct tray_menu_item *it) {
	(void)it;
	g_app_stop = 1;
}

static void activate_screenshot(void) {
	spawn_grabit(ARGS_SHOT_REGION);
}

static const struct tray_menu_item shot_items[] = {
	{.id = 11, .label = "Region", .on_click = click_spawn, .user = ARGS_SHOT_REGION},
	{.id = 12, .label = "Monitor", .on_click = click_spawn, .user = ARGS_SHOT_MONITOR},
	{.id = 13, .label = "Copy", .on_click = click_spawn, .user = ARGS_SHOT_COPY},
	{.id = 14, .label = "Upload", .on_click = click_spawn, .user = ARGS_SHOT_UPLOAD},
	{.id = 15, .label = "Save", .on_click = click_spawn, .user = ARGS_SHOT_SAVE},
	{.id = 16, .label = "Edit", .on_click = click_spawn, .user = ARGS_SHOT_EDIT},
};

static const struct tray_menu_item rec_items[] = {
	{.id = 21, .label = "Region", .on_click = click_spawn, .user = ARGS_REC_REGION},
	{.id = 22, .label = "Monitor", .on_click = click_spawn, .user = ARGS_REC_MONITOR},
};

static const struct tray_menu_item ocr_items[] = {
	{.id = 31, .label = "Copy text", .on_click = click_spawn, .user = ARGS_OCR_COPY},
	{.id = 32, .label = "Show", .on_click = click_spawn, .user = ARGS_OCR_SHOW},
	{.id = 33, .label = "Translate", .on_click = click_spawn, .user = ARGS_OCR_TRANSLATE},
};

static const struct tray_menu_item pin_items[] = {
	{.id = 41, .label = "Pin", .on_click = click_spawn, .user = ARGS_PIN_NEW},
	{.id = 42, .label = "Grab", .on_click = click_spawn, .user = ARGS_PIN_GRAB},
	{.id = 43, .label = "Release", .on_click = click_spawn, .user = ARGS_PIN_RELEASE},
	{.id = 44, .label = "Close all", .on_click = click_spawn, .user = ARGS_PIN_CLOSE},
};

static const struct tray_menu_item app_items[] = {
	{.id = 1, .label = "Screenshot", .children = shot_items, .n_children = sizeof shot_items / sizeof shot_items[0]},
	{.id = 2, .label = "Record", .children = rec_items, .n_children = sizeof rec_items / sizeof rec_items[0]},
	{.id = 3, .label = "OCR", .children = ocr_items, .n_children = sizeof ocr_items / sizeof ocr_items[0]},
	{.id = 4, .label = "Pin", .children = pin_items, .n_children = sizeof pin_items / sizeof pin_items[0]},
	{.id = 5, .label = "Quit", .on_click = click_quit},
};

static const struct tray_menu app_menu = {
	.items = app_items,
	.n = sizeof app_items / sizeof app_items[0],
};

static struct sni_cfg app_sni_cfg = {
	.icon_name = "camera-photo",
	.tooltip_body = "Left click to screenshot, right click for actions",
	.persist = true,
	.on_activate = activate_screenshot,
	.menu = &app_menu,
};

int tray_app_run(struct config *cfg) {
	if (stop_running_tray() == 0) return 0;

	const char *icon = config_get(cfg, "tray.icon");
	if (icon && icon[0]) app_sni_cfg.icon_name = icon;

	pid_t pid = fork();
	if (pid < 0) {
		log_error("tray: fork: %s", strerror(errno));
		return 1;
	}
	if (pid != 0) {
		(void)grabit_waitpid_intr(pid, NULL);
		log_info("tray: started; re-run `grabit --tray` to stop");
		return 0;
	}

	grabit_double_fork_detach();
	if (write_tray_pid() != 0) _exit(0);
	grabit_redirect_stdio_devnull();
	grabit_install_signal_handler(SIGTERM, app_stop_signal);
	grabit_install_signal_handler(SIGINT, app_stop_signal);
	grabit_install_signal_handler(SIGHUP, app_stop_signal);
	grabit_ignore_signal(SIGPIPE);
	sni_run(&g_app_stop, NULL, &app_sni_cfg);
	unlink_tray_pid();
	_exit(0);
}
