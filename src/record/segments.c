// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/segments.h"

#include "log.h"
#include "record/ffmpeg.h"
#include "util.h"
#include "wl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int seg_begin(struct seg_ctx *sc) {
	if (sc->n_segs == sc->cap_segs) {
		size_t cap = sc->cap_segs ? sc->cap_segs * 2 : 4;
		char **p = realloc(sc->segs, cap * sizeof *p);
		if (!p) return -1;
		sc->segs = p;
		sc->cap_segs = cap;
	}
	char *path = NULL;
	if (grabit_xasprintf(&path, "%s.seg%zu.%s",
						 sc->final_path, sc->n_segs, sc->format) != 0)
		return -1;
	if (spawn_ffmpeg(sc->ffmpeg_bin, sc->format, sc->preset, sc->tune, sc->pix_fmt,
					 sc->w, sc->h, sc->fps, sc->crf, path, &sc->pid, &sc->fd) != 0) {
		free(path);
		return -1;
	}
	sc->segs[sc->n_segs++] = path;
	ring_reset(sc->ring);
	sc->es = (struct enc_state){
		.ring = sc->ring,
		.write_fd = sc->fd,
		.stop = sc->stop,
	};
	if (pthread_create(&sc->enc, NULL, encoder_thread, &sc->es) != 0) {
		log_error("pthread_create: %s", strerror(errno));
		close(sc->fd);
		sc->fd = -1;
		(void)wait_ffmpeg(sc->pid);
		sc->pid = -1;
		return -1;
	}
	sc->enc_running = true;
	return 0;
}

void seg_finish(struct seg_ctx *sc, struct grabit_wl_state *s) {
	if (!sc->enc_running) return;
	ring_stop(sc->ring);
	int64_t t0 = grabit_now_ns();
	if (s) {
		while (!atomic_load_explicit(&sc->es.done, memory_order_acquire)) {
			if (grabit_wl_pump(s, 30) != 0) break;
		}
	}
	pthread_join(sc->enc, NULL);
	sc->enc_running = false;
	if (sc->fd >= 0) {
		close(sc->fd);
		sc->fd = -1;
	}
	log_debug("recording: segment drained in %lld ms; ffmpeg finalizes in background",
			  (long long)((grabit_now_ns() - t0) / 1000000));
	if (sc->n_pending == sc->cap_pending) {
		size_t cap = sc->cap_pending ? sc->cap_pending * 2 : 4;
		pid_t *p = realloc(sc->pending, cap * sizeof *p);
		if (!p) {
			if (wait_ffmpeg(sc->pid) != 0) sc->failed = true;
			sc->pid = -1;
			return;
		}
		sc->pending = p;
		sc->cap_pending = cap;
	}
	sc->pending[sc->n_pending++] = sc->pid;
	sc->pid = -1;
}

bool seg_any_pending_alive(struct seg_ctx *sc) {
	bool alive = false;
	for (size_t i = 0; i < sc->n_pending; i++) {
		if (sc->pending[i] <= 0) continue;
		int status = 0;
		pid_t r = waitpid(sc->pending[i], &status, WNOHANG);
		if (r == sc->pending[i]) {
			if (ffmpeg_exit_rc(status) != 0) sc->failed = true;
			sc->pending[i] = -1;
		} else if (r == 0) {
			alive = true;
		}
	}
	return alive;
}

void seg_reap_all(struct seg_ctx *sc) {
	for (size_t i = 0; i < sc->n_pending; i++) {
		if (sc->pending[i] <= 0) continue;
		if (wait_ffmpeg(sc->pending[i]) != 0) sc->failed = true;
	}
	sc->n_pending = 0;
}

static int concat_segments(struct seg_ctx *sc, const char *output_path) {
	char *list_path = NULL;
	if (grabit_xasprintf(&list_path, "%s.concat.txt", output_path) != 0) return -1;
	FILE *fp = fopen(list_path, "w");
	if (!fp) {
		log_error("concat list %s: %s", list_path, strerror(errno));
		free(list_path);
		return -1;
	}
	for (size_t i = 0; i < sc->n_segs; i++) {
		fputs("file '", fp);
		for (const char *p = sc->segs[i]; *p; p++) {
			if (*p == '\'')
				fputs("'\\''", fp);
			else
				fputc(*p, fp);
		}
		fputs("'\n", fp);
	}
	if (fclose(fp) != 0) {
		log_error("concat list %s: %s", list_path, strerror(errno));
		unlink(list_path);
		free(list_path);
		return -1;
	}

	bool gif = strcmp(sc->format, "gif") == 0;
	char *argv[16];
	size_t i = 0;
	argv[i++] = (char *)sc->ffmpeg_bin;
	argv[i++] = (char *)"-nostdin";
	argv[i++] = (char *)"-loglevel";
	argv[i++] = (char *)"error";
	argv[i++] = (char *)"-y";
	argv[i++] = (char *)"-f";
	argv[i++] = (char *)"concat";
	argv[i++] = (char *)"-safe";
	argv[i++] = (char *)"0";
	argv[i++] = (char *)"-i";
	argv[i++] = list_path;
	if (gif) {
		argv[i++] = (char *)"-vf";
		argv[i++] = (char *)GIF_PALETTE_VF;
	} else {
		argv[i++] = (char *)"-c";
		argv[i++] = (char *)"copy";
	}
	argv[i++] = (char *)output_path;
	argv[i] = NULL;

	int rc = ffmpeg_run(sc->ffmpeg_bin, argv, sc->stop);
	unlink(list_path);
	free(list_path);
	if (rc != 0) {
		log_error("ffmpeg concat failed");
		return -1;
	}
	return 0;
}

int seg_assemble(struct seg_ctx *sc, const char *output_path) {
	if (sc->failed || sc->n_segs == 0) return -1;
	if (sc->n_segs == 1) {
		if (rename(sc->segs[0], output_path) != 0) {
			log_error("rename(%s -> %s): %s", sc->segs[0], output_path,
					  strerror(errno));
			return -1;
		}
		return 0;
	}
	if (concat_segments(sc, output_path) != 0) {
		log_error("recording: concat failed; segments kept next to %s", output_path);
		return -1;
	}
	seg_unlink_all(sc);
	return 0;
}

void seg_unlink_all(struct seg_ctx *sc) {
	for (size_t i = 0; i < sc->n_segs; i++)
		unlink(sc->segs[i]);
}

void seg_ctx_free(struct seg_ctx *sc) {
	for (size_t i = 0; i < sc->n_segs; i++)
		free(sc->segs[i]);
	free(sc->segs);
	sc->segs = NULL;
	sc->n_segs = sc->cap_segs = 0;
	free(sc->pending);
	sc->pending = NULL;
	sc->n_pending = sc->cap_pending = 0;
}
