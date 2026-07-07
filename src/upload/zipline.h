// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UPLOAD_ZIPLINE_H
#define GRABIT_UPLOAD_ZIPLINE_H

#include <stdbool.h>

#include <curl/curl.h>

struct config;
struct upload_result;

struct curl_slist *zipline_option_headers(struct config *cfg,
										  struct curl_slist *headers, bool *oom);
int zipline_upload_partial(const char *base_url, const char *auth,
						   struct config *cfg, const char *file_path,
						   struct upload_result *out);

#endif
