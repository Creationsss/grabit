// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_INPUT_STATE_INTERNAL_H
#define GRABIT_REGION_INPUT_STATE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "region/region.h"
#include "region/wlr_state.h"

void gist_undo_apply(struct ro_state *st, const struct undo_item *it);
void gist_undo_record_anno(struct ro_state *st);

#endif
