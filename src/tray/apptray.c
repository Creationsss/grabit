// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/apptray.h"

#include "capture/pixels.h"
#include "config/config.h"

#include "log.h"
#include "paths.h"
#include "tray/menu.h"
#include "tray/sni.h"
#include "ui/icons.h"
#include "util/util.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
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

static void spawn_grabit(const char *const *args) {
	int rc = grabit_fork_detached();
	if (rc < 0) log_warn("tray: could not start grabit");
	if (rc != 0) return;
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
static const char *const ARGS_SETTINGS[] = {"settings", NULL};

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
	{.id = 11, .label = "Region…", .on_click = click_spawn, .user = ARGS_SHOT_REGION},
	{.id = 12, .label = "Monitor…", .on_click = click_spawn, .user = ARGS_SHOT_MONITOR},
	{.id = 13, .label = "Region and copy…", .on_click = click_spawn, .user = ARGS_SHOT_COPY},
	{.id = 14, .label = "Region and upload…", .on_click = click_spawn, .user = ARGS_SHOT_UPLOAD},
	{.id = 15, .label = "Region and save…", .on_click = click_spawn, .user = ARGS_SHOT_SAVE},
	{.id = 16, .label = "Region and edit…", .on_click = click_spawn, .user = ARGS_SHOT_EDIT},
};

static const struct tray_menu_item rec_items[] = {
	{.id = 21, .label = "Region…", .on_click = click_spawn, .user = ARGS_REC_REGION},
	{.id = 22, .label = "Monitor…", .on_click = click_spawn, .user = ARGS_REC_MONITOR},
};

static const struct tray_menu_item ocr_items[] = {
	{.id = 31, .label = "Read text…", .on_click = click_spawn, .user = ARGS_OCR_COPY},
	{.id = 32, .label = "Read and show…", .on_click = click_spawn, .user = ARGS_OCR_SHOW},
	{.id = 33, .label = "Read and translate…", .on_click = click_spawn, .user = ARGS_OCR_TRANSLATE},
};

static const struct tray_menu_item pin_items[] = {
	{.id = 41, .label = "Pin a capture…", .on_click = click_spawn, .user = ARGS_PIN_NEW},
	{.id = 42, .label = "Make pins clickable", .on_click = click_spawn, .user = ARGS_PIN_GRAB},
	{.id = 43, .label = "Make pins click-through", .on_click = click_spawn, .user = ARGS_PIN_RELEASE},
	{.id = 44, .label = "Close all pins", .on_click = click_spawn, .user = ARGS_PIN_CLOSE},
};

static const struct tray_menu_item app_items[] = {
	{.id = 1, .label = "Screenshot", .children = shot_items, .n_children = sizeof shot_items / sizeof shot_items[0]},
	{.id = 2, .label = "Record", .children = rec_items, .n_children = sizeof rec_items / sizeof rec_items[0]},
	{.id = 3, .label = "OCR", .children = ocr_items, .n_children = sizeof ocr_items / sizeof ocr_items[0]},
	{.id = 4, .label = "Pin", .children = pin_items, .n_children = sizeof pin_items / sizeof pin_items[0]},
	{.id = 6, .label = "Settings…", .on_click = click_spawn, .user = ARGS_SETTINGS},
	{.id = 5, .label = "Quit", .on_click = click_quit},
};

static const struct tray_menu app_menu = {
	.items = app_items,
	.n = sizeof app_items / sizeof app_items[0],
};

#define DEFAULT_ICON "camera-photo"

static struct sni_cfg app_sni_cfg = {
	.icon_name = DEFAULT_ICON,
	.tooltip_body = "Left click to screenshot, right click for actions",
	.persist = true,
	.on_activate = activate_screenshot,
	.menu = &app_menu,
};

static const int PIXMAP_SIZES[] = {16, 22, 24, 32, 48, 64};
#define N_PIXMAPS (sizeof PIXMAP_SIZES / sizeof *PIXMAP_SIZES)
static struct sni_pixmap g_pixmaps[N_PIXMAPS];

static bool to_pixmap(cairo_surface_t *s, struct sni_pixmap *out) {
	int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	int stride = cairo_image_surface_get_stride(s);
	cairo_surface_flush(s);
	const unsigned char *src = cairo_image_surface_get_data(s);
	unsigned char *dst = malloc((size_t)w * (size_t)h * 4);
	if (!src || !dst) {
		free(dst);
		return false;
	}
	for (int y = 0; y < h; y++)
		pixels_unpremul_argb_row(dst + (size_t)y * (size_t)w * 4,
								 (const uint32_t *)(src + (size_t)y * (size_t)stride), w);
	*out = (struct sni_pixmap){w, h, dst};
	return true;
}

static bool load_pixmaps(const char *path) {
	cairo_surface_t *surfaces[N_PIXMAPS] = {0};
	size_t n = 0;
	if (ui_icon_render_sizes(path, PIXMAP_SIZES, N_PIXMAPS, surfaces)) {
		for (size_t i = 0; i < N_PIXMAPS; i++) {
			if (surfaces[i] && to_pixmap(surfaces[i], &g_pixmaps[n])) n++;
			if (surfaces[i]) cairo_surface_destroy(surfaces[i]);
		}
	}
	app_sni_cfg.pixmaps = g_pixmaps;
	app_sni_cfg.n_pixmaps = n;
	return n > 0;
}

static char *g_icon;
static int64_t g_cfg_stamp;

static void apply_icon(const char *icon) {
	for (size_t i = 0; i < app_sni_cfg.n_pixmaps; i++)
		free(g_pixmaps[i].argb);
	app_sni_cfg.n_pixmaps = 0;
	app_sni_cfg.icon_name = DEFAULT_ICON;
	free(g_icon);
	g_icon = icon ? strdup(icon) : NULL;
	if (!g_icon) return;
	char *file = ui_icon_file(g_icon);
	if (file) {
		if (load_pixmaps(file))
			app_sni_cfg.icon_name = "";
		else
			log_warn("tray: could not load %s as an icon, using the default icon", file);
		free(file);
		return;
	}
	size_t len = strlen(g_icon);
	if (grabit_utf8_valid_prefix(g_icon, len) == len)
		app_sni_cfg.icon_name = g_icon;
	else
		log_warn("tray: tray.icon is not valid UTF-8, using the default icon");
}

static bool refresh_icon(void) {
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	while (read(app_sni_cfg.watch_fd, buf, sizeof buf) > 0)
		;
	config_watch_add(app_sni_cfg.watch_fd);
	int64_t stamp = config_stamp();
	if (stamp == g_cfg_stamp) return false;
	struct config c;
	if (config_read(&c) != 0) return false;
	g_cfg_stamp = stamp;
	const char *icon = config_get(&c, "tray.icon");
	bool changed = (icon == NULL) != (g_icon == NULL) || (icon && strcmp(icon, g_icon) != 0);
	if (changed) apply_icon(icon);
	config_free(&c);
	return changed;
}

int tray_app_run(struct config *cfg) {
	if (stop_running_tray() == 0) return 0;

	g_cfg_stamp = config_stamp();
	apply_icon(config_get(cfg, "tray.icon"));

	int rc = grabit_fork_detached();
	if (rc < 0) {
		log_error("tray: could not start the tray process");
		return 1;
	}
	if (rc != 0) {
		log_info("tray: started; re-run `grabit --tray` to stop");
		return 0;
	}

	if (write_tray_pid() != 0) _exit(0);
	grabit_redirect_stdio_devnull();
	grabit_install_signal_handler(SIGTERM, app_stop_signal);
	grabit_install_signal_handler(SIGINT, app_stop_signal);
	grabit_install_signal_handler(SIGHUP, app_stop_signal);
	grabit_ignore_signal(SIGPIPE);
	paths_mkdir_p(paths_config_dir());
	app_sni_cfg.watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (app_sni_cfg.watch_fd >= 0) {
		config_watch_add(app_sni_cfg.watch_fd);
		app_sni_cfg.refresh_icon = refresh_icon;
	}
	sni_run(&g_app_stop, NULL, &app_sni_cfg);
	grabit_lock_release(&g_tray_lock_fd, tray_pid_path());
	_exit(0);
}
