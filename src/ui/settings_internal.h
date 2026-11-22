// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_SETTINGS_INTERNAL_H
#define GRABIT_UI_SETTINGS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pthread.h>
#include <stdatomic.h>
#include <sys/types.h>

#include <cairo/cairo.h>

#include "config/config.h"
#include "region/keybinds.h"
#include "ui/icons.h"
#include "ui/scroll.h"
#include "ui/text.h"

struct ui_window;
struct grabit_wl_state;
struct set_upload_test;

#define SET_NONE ((size_t)-1)
#define SET_SIDE_SEARCH ((size_t)-2)
#define SET_SIDE_CLEAR ((size_t)-3)
#define SET_POPUP_PAD_HIT ((size_t)-4)
#define SET_MAX_PAGES 16

#define SET_SIDEBAR_MIN 160
#define SET_SIDEBAR_MAX 220
#define SET_SIDE_ROW_H 32
#define SET_PAD 10
#define SET_SEARCH_H 30
#define SET_SIDE_LIST_Y (SET_PAD * 2 + SET_SEARCH_H)

#define SET_BODY_PAD 24
#define SET_TITLE_Y 18
#define SET_ROW_PAD 12
#define SET_SECTION_H 34
#define SET_CTRL_H 30
#define SET_BTN_W 30
#define SET_GAP 4
#define SET_LABEL_SIZE 13.5
#define SET_HINT_SIZE 11.5
#define SET_HINT_LINE 15.0
#define SET_HINT_MAX_LINES 3
#define SET_POPUP_ITEM_H 28
#define SET_POPUP_MAX_ITEMS 10
#define SET_MENU_W 200
#define SET_POPUP_FILTER_H 36
#define SET_ICON_PX 18
#define SET_ICON_CACHE 48
#define SET_TIP_DELAY_MS 600
#define SET_DOUBLE_CLICK_MS 400
#define SET_TOAST_MS 2500
#define SET_UNDO_TOAST_MS 5000
#define SET_HISTORY 50
#define SET_RESET_W 26
#define SET_JOB_POLL_MS 200
#define SET_ROW_BTN_W 84

enum set_part {
	SP_NONE,
	SP_ROW,
	SP_CONTROL,
	SP_LABEL,
	SP_MINUS,
	SP_PLUS,
	SP_AUX,
	SP_RESET,
	SP_ALT,
	SP_ALT2,
};

enum set_aux {
	AUX_NONE,
	AUX_EYE,
	AUX_RECORD,
	AUX_SWATCH,
};

enum set_popup {
	POPUP_NONE,
	POPUP_SELECT,
	POPUP_MENU,
};

enum set_menu_item {
	MENU_RESET,
	MENU_COPY_KEY,
	MENU_COUNT,
};

enum set_area {
	AREA_SIDEBAR,
	AREA_SEARCH,
	AREA_BODY,
};

struct settings_state;
struct set_row;

#define SET_ALT_BTNS 2
#define SET_UPDATE_ALL "Update all"

typedef void (*set_row_action)(struct settings_state *st, struct set_row *r);
typedef void (*set_toast_action)(struct settings_state *st, const char *arg);

struct set_row {
	const char *key;
	char label[64];
	const char *hint;
	struct config_key_info info;
	char **choices;
	size_t n_choices;
	size_t choices_cap;
	const char *empty_label;
	const char *page;
	const char *section;
	bool section_head;
	const struct set_dep *dep;
	enum set_aux aux;
	bool reveal;
	bool no_config;
	const char *button;
	set_row_action action;
	struct {
		const char *label;
		set_row_action action;
	} alt[SET_ALT_BTNS];
	bool (*submit)(struct settings_state *st, const char *value);
	char *owned_key;
	char *owned_hint;
	bool icons_loaded;
	char error[256];
	char note[160];
	size_t note_more;
	double y;
	double h;
};

struct set_dep {
	const char *key;
	const char *needs;
	const char *values;
	const char *reason;
};

struct set_page {
	const char *title;
	const char *note;
	struct set_row **rows;
	size_t n;
	size_t cap;
};

struct set_rect {
	double x;
	double y;
	double w;
	double h;
};

enum set_icons_state {
	ICONS_IDLE,
	ICONS_SCANNING,
	ICONS_READY,
};

struct set_icon_slot {
	const char *name;
	int px;
	cairo_surface_t *surface;
};

struct set_list {
	set_row_action action;
	set_row_action alt[SET_ALT_BTNS];
	const char *prefix;
	const char *suffix;
	const char *section;
	const char *button;
	const char *unreadable;
	void (*describe)(struct settings_state *st, struct set_row *r, const char *name);
};

struct set_plugin_jobs {
	bool install;
	pid_t pid;
	char **names;
	size_t n;
	size_t next;
	size_t failed;
	const char *failed_name;
};

#define SET_DIALOG_BTNS 2

struct set_dialog_btn {
	const char *label;
	bool danger;
	set_toast_action fn;
	struct set_rect rect;
};

struct set_dialog {
	bool open;
	char title[96];
	char text[256];
	char *arg;
	struct set_dialog_btn btn[SET_DIALOG_BTNS];
	size_t n_btn;
	const char *check_label;
	bool check_on;
	const char *check_key;
	struct set_rect check_rect;
	size_t hover;
	size_t press;
	size_t focus;
};

struct set_change {
	struct set_row *row;
	char *before;
	char *after;
	char *sxcu;
	bool chain;
};

struct settings_state {
	struct ui_window *win;
	struct grabit_wl_state *wls;
	struct config cfg;
	int64_t cfg_stamp;
	int64_t state_stamp;

	struct set_page all;
	struct set_page pages[SET_MAX_PAGES];
	size_t n_pages;
	struct set_page results;
	size_t page;

	struct ui_text search;
	enum set_area area;
	size_t side_hover;
	struct ui_scroll side;
	struct ui_scroll body;

	size_t focus;
	bool focus_visible;
	bool editing;
	bool text_drag;
	struct ui_text edit;
	int64_t last_click_ms;
	size_t last_click_row;

	size_t hover_row;
	enum set_part hover_part;
	size_t press_row;
	enum set_part press_part;

	enum set_popup popup;
	size_t popup_row;
	size_t popup_hover;
	double popup_offset;
	double popup_x;
	double popup_y;
	struct ui_text popup_filter;
	size_t *popup_map;
	size_t popup_map_n;
	size_t popup_map_cap;

	struct ui_icons icons;
	enum set_icons_state icons_state;
	atomic_bool icons_done;
	struct ui_icon_scan *icon_scan;
	pthread_t icons_thread;
	struct set_icon_slot icon_cache[SET_ICON_CACHE];
	size_t icon_cache_next;

	size_t tip_row;
	enum set_part tip_part;
	int64_t tip_at;
	char tip_buf[160];

	char toast[192];
	int64_t toast_until;
	const char *toast_label;
	bool toast_danger;
	set_toast_action toast_fn;
	char *toast_arg;
	struct set_rect toast_btn;
	size_t uploads_page;
	size_t plugins_page;
	struct set_plugin_jobs plugin_jobs;
	struct set_upload_test *upload_test;
	struct set_dialog dialog;
	struct {
		struct set_row *row;
		struct keybind_list binds;
	} capture;
	struct set_rect page_reset_btn;
	bool page_reset_hover;

	struct set_change history[SET_HISTORY];
	size_t history_n;
	size_t history_pos;
	bool drag_over;

	char banner[256];
	double view_w;
	double view_h;
};

static inline bool set_masked(const struct set_row *r) {
	return r->info.secret && !r->reveal;
}

int settings_state_open(struct settings_state *st, struct grabit_wl_state *wls);
void settings_state_close(struct settings_state *st);
int settings_build(struct settings_state *st);
struct set_row *set_find_row(struct settings_state *st, const char *key);
void settings_free_rows(struct settings_state *st);
void settings_filter(struct settings_state *st);

struct set_page *set_current_page(struct settings_state *st);
const char *set_value(struct settings_state *st, const struct set_row *r);
const char *set_effective(struct settings_state *st, const struct set_row *r);
bool set_is_custom(struct settings_state *st, const struct set_row *r);
bool set_can_reset(struct settings_state *st, const struct set_row *r);
bool set_step_enabled(struct settings_state *st, const struct set_row *r, int dir);
const char *set_inactive_reason(struct settings_state *st, const struct set_row *r);
void set_default_text(const struct set_row *r, char *out, size_t n);
size_t set_page_matches(const struct settings_state *st, size_t page);
size_t set_dd_count(const struct set_row *r);
const char *set_dd_value(const struct set_row *r, size_t i);
const char *set_dd_label(const struct set_row *r, size_t i);
size_t set_dd_index_of(struct settings_state *st, const struct set_row *r);

double set_sidebar_w(double window_w);
void set_layout(struct settings_state *st);
double set_body_x(const struct settings_state *st);
double set_body_w(const struct settings_state *st);
bool set_part_rect(struct settings_state *st, const struct set_row *r, enum set_part part,
				   struct set_rect *out);
size_t set_row_at(struct settings_state *st, double y);
enum set_part set_part_at(struct settings_state *st, const struct set_row *r, double x,
						  double y);
size_t set_popup_count(struct settings_state *st);
size_t set_popup_choice(const struct settings_state *st, size_t i);
bool set_popup_filtered(struct settings_state *st);
void set_popup_refilter(struct settings_state *st);
bool set_row_has_icons(const struct set_row *r);
void set_icons_start(struct settings_state *st);
bool set_icons_ensure(struct settings_state *st, struct set_row *r, bool block);
bool set_icons_poll(struct settings_state *st);
cairo_surface_t *set_icon(struct settings_state *st, const char *name, int px);
void set_icons_free(struct settings_state *st);
const char *set_popup_label(struct settings_state *st, size_t i);
bool set_popup_enabled(struct settings_state *st, size_t i);
bool set_popup_rect(struct settings_state *st, struct set_rect *out, size_t *visible);
size_t set_popup_item_at(struct settings_state *st, double bx, double by);
size_t set_side_hit(const struct settings_state *st, double x, double y);
bool set_search_clear_rect(const struct settings_state *st, struct set_rect *out);
const char *set_tip_text(struct settings_state *st, size_t row, enum set_part part);
bool set_rect_contains(const struct set_rect *r, double x, double y);
double set_content_h(struct settings_state *st);

void set_draw(struct ui_window *w, cairo_t *cr, double width, double height);

void set_on_button(struct ui_window *w, double x, double y, uint32_t button, bool pressed);
void set_on_timer(struct ui_window *w);
void set_arm_timer(struct settings_state *st);
bool set_tip_visible(const struct settings_state *st);
void set_on_motion(struct ui_window *w, double x, double y);
void set_on_leave(struct ui_window *w);
void set_on_scroll(struct ui_window *w, double x, double y, double units);
bool set_on_key(struct ui_window *w, xkb_keysym_t sym, uint8_t mods);
void set_on_focus_lost(struct ui_window *w);

void set_apply(struct settings_state *st, struct set_row *r, const char *value);
void set_undo(struct settings_state *st);
void set_redo(struct settings_state *st);
size_t set_page_resettable(struct settings_state *st, const struct set_page *p);
void set_reset_page_ask(struct settings_state *st);
void set_toast_run(struct settings_state *st);
void set_ask(struct settings_state *st, const char *label, bool danger, set_toast_action fn,
			 char *arg);
void set_test_upload(struct settings_state *st, struct set_row *r);
bool set_tests_poll(struct settings_state *st);
void set_tests_free(struct settings_state *st);
void set_history_free(struct settings_state *st);
struct set_row *set_submit_row(struct settings_state *st,
							   bool (*fn)(struct settings_state *st, const char *value));
struct set_row *set_sxcu_row(struct settings_state *st);
void set_toast(struct settings_state *st, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void set_refresh_uploaders(struct settings_state *st);
void set_remove_uploader_ask(struct settings_state *st, struct set_row *r);
void set_sync_list(struct settings_state *st, size_t page, const struct set_list *l,
				   char **names, size_t n);
void set_refresh_plugins(struct settings_state *st);
void set_refresh_cfg(struct settings_state *st);
void set_remove_unknown_ask(struct settings_state *st, struct set_row *r);
void set_rename_unknown(struct settings_state *st, struct set_row *r);
void set_dialog_open(struct settings_state *st, const char *title, const char *text, char *arg);
void set_dialog_button(struct settings_state *st, const char *label, bool danger,
					   set_toast_action fn);
void set_dialog_check(struct settings_state *st, const char *label, const char *key);
void set_dialog_close(struct settings_state *st);
void set_dialog_draw(struct settings_state *st, cairo_t *cr, double width, double height);
bool set_dialog_press(struct settings_state *st, double x, double y, uint32_t button,
					  bool pressed);
bool set_dialog_motion(struct settings_state *st, double x, double y);
bool set_dialog_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods);
void set_remove_plugin_ask(struct settings_state *st, struct set_row *r);
void set_update_plugins(struct settings_state *st, struct set_row *r);
bool set_jobs_poll(struct settings_state *st);
void set_jobs_free(struct settings_state *st);
void set_refresh_notes(struct settings_state *st);
bool set_add_sxcu(struct settings_state *st, const char *path_or_uri);
bool set_install_plugin(struct settings_state *st, const char *source);
char *set_resolve_path(const char *path_or_uri);

struct set_drop_hint {
	const char *title;
	const char *sub;
	const char *refused;
};
const struct set_drop_hint *set_drop_hint(const struct settings_state *st);
void set_on_drag(struct ui_window *w, bool over);
void set_on_drop(struct ui_window *w, const char *uri_list);
void set_commit_edit(struct settings_state *st);

void set_capture_start(struct settings_state *st, struct set_row *r);
void set_capture_end(struct settings_state *st, bool save);
bool set_capture_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods);
bool set_capture_button(struct settings_state *st, uint32_t button, bool pressed);

#endif
