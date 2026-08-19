// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAIRO_UTIL_H
#define GRABIT_CAIRO_UTIL_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline void grabit_cairo_set_source_argb(cairo_t *cr, uint32_t color, double alpha) {
	double r = ((color >> 16) & 0xff) / 255.0;
	double g = ((color >> 8) & 0xff) / 255.0;
	double b = (color & 0xff) / 255.0;
	cairo_set_source_rgba(cr, r, g, b, alpha);
}

static inline cairo_surface_t *grabit_cairo_image(void *data, cairo_format_t fmt,
												  int32_t w, int32_t h, int32_t stride) {
	cairo_surface_t *s = cairo_image_surface_create_for_data(data, fmt, w, h, stride);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		return NULL;
	}
	return s;
}

static inline cairo_surface_t *grabit_cairo_image_argb(void *data, int32_t w, int32_t h,
													   int32_t stride) {
	return grabit_cairo_image(data, CAIRO_FORMAT_ARGB32, w, h, stride);
}

static inline void grabit_cairo_rounded_rect(cairo_t *cr, double x, double y,
											 double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2.0 * M_PI);
	cairo_arc(cr, x + w - r, y + h - r, r, 0.0, 0.5 * M_PI);
	cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
	cairo_close_path(cr);
}

struct grabit_cairo_path_cmd {
	int type;
	double x, y;
	double x1, y1, x2, y2, x3, y3;
};

static inline void grabit_cairo_path_fillet_corners(cairo_t *cr, const cairo_path_t *path, double radius) {
	if (!path || path->num_data == 0) return;

	cairo_new_path(cr);
	int i = 0;
	while (i < path->num_data) {
		const cairo_path_data_t *header = &path->data[i];
		if (header->header.type != CAIRO_PATH_MOVE_TO) {
			i += header->header.length;
			continue;
		}

		int start_idx = i;
		int j = i;
		while (j < path->num_data) {
			const cairo_path_data_t *d = &path->data[j];
			if (d->header.type == CAIRO_PATH_CLOSE_PATH) {
				j += d->header.length;
				break;
			}
			if (j > i && d->header.type == CAIRO_PATH_MOVE_TO) break;
			j += d->header.length;
		}

		struct grabit_cairo_path_cmd cmds[256];
		int ncmds = 0;
		int curr = start_idx;
		while (curr < j && ncmds < 256) {
			const cairo_path_data_t *d = &path->data[curr];
			if (d->header.type == CAIRO_PATH_MOVE_TO) {
				cmds[ncmds] = (struct grabit_cairo_path_cmd){.type = CAIRO_PATH_MOVE_TO, .x = d[1].point.x, .y = d[1].point.y};
				ncmds++;
			} else if (d->header.type == CAIRO_PATH_LINE_TO) {
				cmds[ncmds] = (struct grabit_cairo_path_cmd){.type = CAIRO_PATH_LINE_TO, .x = d[1].point.x, .y = d[1].point.y};
				ncmds++;
			} else if (d->header.type == CAIRO_PATH_CURVE_TO) {
				cmds[ncmds] = (struct grabit_cairo_path_cmd){
					.type = CAIRO_PATH_CURVE_TO,
					.x1 = d[1].point.x,
					.y1 = d[1].point.y,
					.x2 = d[2].point.x,
					.y2 = d[2].point.y,
					.x3 = d[3].point.x,
					.y3 = d[3].point.y,
					.x = d[3].point.x,
					.y = d[3].point.y,
				};
				ncmds++;
			} else if (d->header.type == CAIRO_PATH_CLOSE_PATH) {
				cmds[ncmds] = (struct grabit_cairo_path_cmd){.type = CAIRO_PATH_CLOSE_PATH};
				ncmds++;
			}
			curr += d->header.length;
		}

		if (ncmds > 1) {
			double prev_x = 0, prev_y = 0;
			double start_x = 0, start_y = 0;
			for (int k = 0; k < ncmds; k++) {
				if (cmds[k].type == CAIRO_PATH_MOVE_TO) {
					cairo_move_to(cr, cmds[k].x, cmds[k].y);
					prev_x = start_x = cmds[k].x;
					prev_y = start_y = cmds[k].y;
				} else if (cmds[k].type == CAIRO_PATH_LINE_TO) {
					int next_k = (k + 1 < ncmds) ? k + 1 : -1;
					double next_x = 0, next_y = 0;
					bool has_next_corner = false;

					if (next_k >= 0 && cmds[next_k].type == CAIRO_PATH_LINE_TO) {
						next_x = cmds[next_k].x;
						next_y = cmds[next_k].y;
						has_next_corner = true;
					} else if (next_k >= 0 && cmds[next_k].type == CAIRO_PATH_CLOSE_PATH && ncmds > 2) {
						next_x = start_x;
						next_y = start_y;
						has_next_corner = true;
					}

					if (has_next_corner) {
						double bx = cmds[k].x, by = cmds[k].y;
						double v1x = bx - prev_x, v1y = by - prev_y;
						double v2x = next_x - bx, v2y = next_y - by;
						double l1 = sqrt(v1x * v1x + v1y * v1y);
						double l2 = sqrt(v2x * v2x + v2y * v2y);

						if (l1 > 0.5 && l2 > 0.5) {
							double u1x = v1x / l1, u1y = v1y / l1;
							double u2x = v2x / l2, u2y = v2y / l2;
							double dot = u1x * u2x + u1y * u2y;

							if (dot < 0.98 && dot > -0.99) {
								double d = fmin(radius, fmin(l1 * 0.45, l2 * 0.45));
								double p_start_x = bx - u1x * d;
								double p_start_y = by - u1y * d;
								double p_end_x = bx + u2x * d;
								double p_end_y = by + u2y * d;

								cairo_line_to(cr, p_start_x, p_start_y);

								double cp1x = bx - u1x * (d * 0.45);
								double cp1y = by - u1y * (d * 0.45);
								double cp2x = bx + u2x * (d * 0.45);
								double cp2y = by + u2y * (d * 0.45);
								cairo_curve_to(cr, cp1x, cp1y, cp2x, cp2y, p_end_x, p_end_y);

								prev_x = p_end_x;
								prev_y = p_end_y;
								continue;
							}
						}
					}
					cairo_line_to(cr, cmds[k].x, cmds[k].y);
					prev_x = cmds[k].x;
					prev_y = cmds[k].y;
				} else if (cmds[k].type == CAIRO_PATH_CURVE_TO) {
					cairo_curve_to(cr, cmds[k].x1, cmds[k].y1, cmds[k].x2, cmds[k].y2, cmds[k].x3, cmds[k].y3);
					prev_x = cmds[k].x3;
					prev_y = cmds[k].y3;
				} else if (cmds[k].type == CAIRO_PATH_CLOSE_PATH) {
					cairo_close_path(cr);
				}
			}
		}

		i = j;
	}
}

#endif
