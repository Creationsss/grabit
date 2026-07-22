// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UPLOAD_INTERNAL_H
#define GRABIT_UPLOAD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

struct config;

struct service {
	const char *name;
	const char *url;
	const char *auth_name;
	const char *json_path;
	bool auth_in_form;
};

const struct service *gup_find_service(const char *name);
const char *gup_resolve_auth(struct config *cfg, const char *service);

#endif
