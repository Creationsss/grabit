// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "sound/sound.h"

#include "config/config.h"
#include "log.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *find_player(const char *configured) {
	if (configured && configured[0] && grabit_in_path(configured)) return configured;
	static const char *const CANDIDATES[] = {"pw-play", "paplay", "play", "aplay", NULL};
	for (size_t i = 0; CANDIDATES[i]; i++) {
		if (grabit_in_path(CANDIDATES[i])) return CANDIDATES[i];
	}
	return NULL;
}

static const char *find_sound_file(const char *configured) {
	if (configured && configured[0] && access(configured, R_OK) == 0) return configured;
	static const char *const CANDIDATES[] = {
		"/usr/share/sounds/freedesktop/stereo/camera-shutter.oga",
		"/usr/share/sounds/freedesktop/stereo/screen-capture.oga",
		"/usr/share/sounds/sound-theme-freedesktop/stereo/camera-shutter.oga",
		"/usr/share/sounds/freedesktop/stereo/message-new-instant.oga",
		NULL,
	};
	for (size_t i = 0; CANDIDATES[i]; i++) {
		if (access(CANDIDATES[i], R_OK) == 0) return CANDIDATES[i];
	}
	return NULL;
}

static bool g_warned_player = false;
static bool g_warned_file = false;

void grabit_sound_play(struct config *cfg) {
	if (log_is_silent()) return;
	const char *enabled = config_get(cfg, "sound.enabled");
	if (!enabled || strcmp(enabled, "true") != 0) return;

	const char *player = find_player(config_get(cfg, "sound.player"));
	if (!player) {
		if (!g_warned_player) {
			log_warn("sound: no audio player in $PATH; set one with `grabit set "
					 "sound.player <path>`");
			g_warned_player = true;
		}
		return;
	}

	const char *file = find_sound_file(config_get(cfg, "sound.file"));
	if (!file) {
		if (!g_warned_file) {
			log_warn("sound: no sound file found; set one with `grabit set "
					 "sound.file <path>`");
			g_warned_file = true;
		}
		return;
	}

	pid_t pid = fork();
	if (pid < 0) return;
	if (pid == 0) {
		grabit_double_fork_detach();
		grabit_redirect_stdio_devnull();
		char *argv[] = {(char *)player, (char *)file, NULL};
		execvp(player, argv);
		_exit(127);
	}
	(void)grabit_waitpid_intr(pid, NULL);
}
