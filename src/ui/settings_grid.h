// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_SETTINGS_GRID_H
#define GRABIT_SETTINGS_GRID_H

#include "ui/settings_internal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

#define GRID_TILE 150.0
#define GRID_GAP 14.0
#define GRID_CAPTION 40.0
#define GRID_HEAD_H 34.0
#define GRID_WRAP_W 256
#define GRID_FIELD_H 32.0
#define GRID_CHIP_H 28.0
#define GRID_ROW_STEP (GRID_TILE + GRID_CAPTION + GRID_GAP)
#define GRID_CACHE_PX 512
#define GRID_PLAY_FPS 30

enum grid_thumb_state { TH_NONE,
						TH_LOADING,
						TH_DONE,
						TH_SKIP };

struct grid_thumbs {
	atomic_int refs;
	bool running;
	bool quit;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	atomic_bool stop;
	int px;
	size_t n;
	char **ids;
	char **paths;
	char **urls;
	bool *media;
	int *secs;
	char *ffmpeg;
	atomic_uchar *state;
	cairo_surface_t **thumbs;
	size_t *want;
	size_t n_want;
	size_t cap_want;
	atomic_size_t finished;
	size_t seen;
};

enum grid_type { GT_ALL,
				 GT_UPLOADS,
				 GT_SAVED,
				 GT_RECORDINGS,
				 GT_COUNT };
enum grid_date { GD_ANY,
				 GD_TODAY,
				 GD_WEEK,
				 GD_MONTH,
				 GD_YEAR,
				 GD_COUNT };
enum grid_size { GS_ANY,
				 GS_SMALL,
				 GS_MEDIUM,
				 GS_LARGE,
				 GS_COUNT };

enum grid_stop {
	GF_SEARCH,
	GF_TYPE,
	GF_SERVICE = GF_TYPE + GT_COUNT,
	GF_DATE,
	GF_SIZE,
	GF_CLEAR,
	GF_COUNT,
};

struct view_job;
struct media_player;
struct media_job;

struct media_info {
	int secs;
	int w;
	int h;
};

struct grid_item {
	double x;
	double y;
	size_t entry;
};

struct grid_head {
	double y;
	char label[48];
};

struct set_hist_grid {
	struct grid_thumbs *th;
	int64_t *sizes;
	size_t n_sizes;

	struct ui_text query;
	bool typing;
	enum grid_type type;
	enum grid_date date;
	enum grid_size size;
	char service[64];

	struct grid_item *items;
	size_t n_items;
	struct grid_head *heads;
	size_t n_heads;
	double body_h;
	double laid_w;
	bool dirty;

	double bar_h;
	double bar_y;
	struct set_rect stops[GF_COUNT];
	struct set_rect clear_btn;
	size_t bar_hover;
	size_t focus_stop;

	char svc_labels[SET_MAX_ACTIONS][64];
	bool tile_menu;
	size_t hover;
	int64_t hover_at;
	bool previewed;
	size_t sel;
	cairo_surface_t *big;
	char *big_id;

	char *view_id;
	size_t view;
	cairo_surface_t *view_img;
	char *view_img_id;
	struct view_job *job;
	char *ffmpeg;
	struct media_player *player;
	cairo_surface_t *frame;
	bool play_failed;
	struct set_rect img;
	struct set_hist_action acts[SET_MAX_ACTIONS];
	size_t n_acts;
	struct set_rect btn[SET_MAX_ACTIONS];
	struct set_rect panel;
	struct set_rect prev;
	struct set_rect next;
	struct set_rect close;
	size_t view_hover;
	size_t view_press;
	size_t view_focus;
	size_t view_ran;
	int64_t view_tip_at;
	bool tip_drawn;
};

bool grid_is_image(const char *path);
cairo_surface_t *grid_fit(cairo_surface_t *src, int max);
cairo_surface_t *grid_load_fit(const char *path, int max);
int grid_fetch(atomic_bool *stop, const char *url, const char *dest);
void grid_temp_path(const char *base, char *out, size_t n);
void grid_mark_none(const char *id);

struct grid_thumbs *thumbs_new(const struct history_list *h, struct grid_thumbs *old, int px,
							   const char *ffmpeg);
void thumbs_start(struct grid_thumbs *t);
void grid_thread_enter(void);
void grid_thread_leave(void);
void grid_threads_wait(int max_ms);
void thumbs_release(struct grid_thumbs *t);
int thumbs_px(const struct grid_thumbs *t);
void thumbs_want(struct grid_thumbs *t, const size_t *idx, size_t n);
cairo_surface_t *thumbs_get(struct grid_thumbs *t, size_t i);
bool thumbs_pending(struct grid_thumbs *t, size_t i);
bool thumbs_poll(struct grid_thumbs *t);
bool thumbs_busy(struct grid_thumbs *t);
int thumbs_secs(struct grid_thumbs *t, size_t i);

bool media_is_url(const char *s);
bool media_path(const char *path);
const char *media_source(const struct history_entry *e);
bool media_entry(const struct history_entry *e);
bool media_is_gif(const struct history_entry *e);
char *media_ffmpeg(struct config *cfg);
void media_secs_text(int secs, char *out, size_t n);
int media_grab(const char *ffmpeg, const char *src, const char *dest, int max, int timeout_ms,
			   atomic_bool *stop, struct media_info *info);
cairo_surface_t *media_poster(const char *ffmpeg, const char *id, const char *src,
							  atomic_bool *stop, struct media_info *info);
struct media_job *media_job_start(const char *ffmpeg, const char *path, int max);
bool media_job_take(struct media_job *j, cairo_surface_t **out);
void media_job_end(struct media_job *j);
struct media_player *player_start(const char *ffmpeg, const char *id, const char *src, int box_w,
								  int box_h);
bool player_frame(struct media_player *p, cairo_surface_t **surf);
void player_toggle(struct media_player *p);
bool player_paused(const struct media_player *p);
bool player_failed(const struct media_player *p);
int player_secs(struct media_player *p);
void player_stop(struct media_player *p);

void grid_draw_image(cairo_t *cr, cairo_surface_t *s, double x, double y, double w, double h,
					 double rad, bool cover);
void grid_play(cairo_t *cr, double cx, double cy, double size, double alpha);
void grid_centered(cairo_t *cr, double x, double cy, double w, const char *text, double size,
				   double alpha);
size_t grid_wrap(cairo_t *cr, const char *text, double w, double size, char lines[][GRID_WRAP_W],
				 size_t max);
void grid_focus_ring(cairo_t *cr, struct set_rect r, bool round);
void grid_size_text(int64_t bytes, char *out, size_t n);
size_t grid_order_pos(const struct set_hist_grid *g, size_t entry);

extern const char *const GRID_TYPE_LABELS[GT_COUNT];
extern const char *const GRID_DATE_LABELS[GD_COUNT];
extern const char *const GRID_SIZE_LABELS[GS_COUNT];

int grid_want_px(const struct settings_state *st);
bool grid_filtered(const struct set_hist_grid *g);
void grid_relayout(struct settings_state *st);
const char *grid_service_label(const struct set_hist_grid *g);
double grid_items_top(const struct set_hist_grid *g, const struct set_row *r);
struct set_rect grid_tile_rect(const struct set_hist_grid *g, const struct set_row *r, size_t pos);
double grid_bar_top(const struct settings_state *st, const struct set_row *r);
size_t grid_tile_at(struct settings_state *st, const struct set_row *r, double bx, double by);
size_t grid_bar_at(struct settings_state *st, const struct set_row *r, double bx, double by);
bool grid_preview_due(const struct settings_state *st);
const struct set_row *grid_row(struct settings_state *st, size_t *index);

void viewer_show(struct settings_state *st, size_t entry);
void viewer_close(struct settings_state *st);
void viewer_free(struct set_hist_grid *g);
bool viewer_poll(struct settings_state *st);
bool viewer_busy(const struct settings_state *st);

#endif
