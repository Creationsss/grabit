// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_SETTINGS_INPUT_H
#define GRABIT_SETTINGS_INPUT_H

#include "ui/settings_internal.h"

#include <stdbool.h>
#include <stddef.h>

struct set_row *set_focus_row(struct settings_state *st);
void set_close_popup(struct settings_state *st);
void set_hide_tip(struct settings_state *st);
void set_reveal_focus(struct settings_state *st);
void set_popup_reveal(struct settings_state *st);
void set_open_menu(struct settings_state *st, size_t row, double bx, double by);
void set_cancel_edit(struct settings_state *st);
bool set_leave_edit(struct settings_state *st);
void set_step_int(struct settings_state *st, struct set_row *r, int dir, bool big);
void set_step_enum(struct settings_state *st, struct set_row *r, int dir);
void set_toggle(struct settings_state *st, struct set_row *r);
void set_activate(struct settings_state *st, size_t row);
void set_popup_choose(struct settings_state *st, size_t idx);
void set_select_page(struct settings_state *st, size_t i);
void set_search_changed(struct settings_state *st);

#endif
