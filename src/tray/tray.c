// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/tray.h"

#include "log.h"
#include "notify/notify.h"
#include "record/loop.h"
#include "tray/menu.h"
#include "tray/sni.h"
#include "util/util.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

struct tray_state {
	pid_t pid;
};

static volatile sig_atomic_t g_tray_stop = 0;
static volatile sig_atomic_t g_tray_layout_update = 0;

static void tray_signal(int sig) {
	(void)sig;
	g_tray_stop = 1;
}

static void tray_layout_signal(int sig) {
	(void)sig;
	g_tray_layout_update = 1;
}

static void install_signals(void) {
	grabit_install_signal_handler(SIGTERM, tray_signal);
	grabit_install_signal_handler(SIGINT, tray_signal);
	grabit_install_signal_handler(SIGHUP, tray_signal);
	grabit_install_signal_handler(SIGUSR2, tray_layout_signal);
	grabit_ignore_signal(SIGPIPE);
}

pid_t tray_get_pid(const struct tray_state *t) {
	return t ? t->pid : 0;
}

static void signal_parent(int sig) {
	pid_t parent = getppid();
	if (parent > 1) kill(parent, sig);
}

static const char *pause_label(void) {
	return atomic_load(&grabit_rec_pause) ? "Continue" : "Pause";
}

static void click_pause(const struct tray_menu_item *it) {
	(void)it;
	signal_parent(SIGUSR1);
}

static void click_stop(const struct tray_menu_item *it) {
	(void)it;
	signal_parent(SIGINT);
}

static void click_abort(const struct tray_menu_item *it) {
	(void)it;
	signal_parent(SIGQUIT);
}

static void activate_stop(void) {
	signal_parent(SIGINT);
}

static const struct tray_menu_item rec_menu_items[] = {
	{.id = 1, .label_fn = pause_label, .on_click = click_pause},
	{.id = 2, .label = "Stop", .on_click = click_stop},
	{.id = 3, .label = "Abort", .on_click = click_abort},
};

static const struct tray_menu rec_menu = {
	.items = rec_menu_items,
	.n = sizeof rec_menu_items / sizeof rec_menu_items[0],
};

static const struct sni_cfg rec_sni_cfg = {
	.icon_name = "media-record",
	.tooltip_body = "Left click to stop, Right click for options",
	.persist = false,
	.on_activate = activate_stop,
	.menu = &rec_menu,
};

struct tray_state *tray_start(void) {
	struct tray_state *t = calloc(1, sizeof *t);
	if (!t) return NULL;

	grabit_ignore_signal(SIGUSR2);
	pid_t pid = fork();
	if (pid < 0) {
		log_warn("tray: fork failed: %s", strerror(errno));
		notify_send(&(struct notify_opts){
			.summary = "grabit: tray icon unavailable",
			.body = "could not spawn the tray process (transient); recording continues without it",
		});
		free(t);
		return NULL;
	}
	if (pid == 0) {
		setpgid(0, 0);
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		if (getppid() == 1) _exit(0);
		install_signals();
		sni_run(&g_tray_stop, &g_tray_layout_update, &rec_sni_cfg);
		_exit(0);
	}
	(void)setpgid(pid, pid);
	t->pid = pid;
	return t;
}

void tray_stop(struct tray_state *t) {
	if (!t) return;
	if (t->pid > 0) {
		kill(t->pid, SIGTERM);
		(void)grabit_waitpid_intr(t->pid, NULL);
	}
	free(t);
}
