// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "args.h"
#include "config/config.h"
#include "log.h"
#include "notify/notify.h"
#include "upload/internal.h"
#include "upload/sxcu.h"
#include "upload/zipline.h"
#include "util/json_path.h"
#include "util/util.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#include "version.h"

size_t upload_curl_buf_write(char *ptr, size_t size, size_t nmemb, void *user) {
	struct grabit_buf *b = user;
	size_t total = size * nmemb;
	return grabit_buf_putn(b, ptr, total) == 0 ? total : 0;
}

void upload_curl_common(CURL *curl) {
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "grabit/" GRABIT_VERSION);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
					 (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
					 (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
}

static char *extract_url(struct json_object *root, const char *paths) {
	const char *p = paths;
	while (*p) {
		const char *bar = strchr(p, '|');
		size_t len = bar ? (size_t)(bar - p) : strlen(p);
		char one[256];
		if (len >= sizeof one) return NULL;
		memcpy(one, p, len);
		one[len] = '\0';
		char *got = grabit_json_path_string(root, one);
		if (got) return got;
		if (!bar) break;
		p = bar + 1;
	}
	return NULL;
}

static size_t value_clean_len(const char *value) {
	size_t n = strlen(value);
	while (n > 0 && (value[n - 1] == '\r' || value[n - 1] == '\n' ||
					 value[n - 1] == ' ' || value[n - 1] == '\t'))
		n--;
	for (size_t i = 0; i < n; i++) {
		if (value[i] == '\r' || value[i] == '\n') return 0;
	}
	return n;
}

static bool header_name_ok(const char *name) {
	if (!name || !*name) return false;
	for (const char *p = name; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c <= 0x20 || c == 0x7f || c == ':') return false;
	}
	return true;
}

struct curl_slist *upload_header_append(struct curl_slist *list, const char *name,
										const char *value, bool *oom) {
	static bool warned_name, warned_value;
	if (!header_name_ok(name)) {
		if (!warned_name) {
			warned_name = true;
			log_warn("upload: dropping header with invalid name `%s`", name ? name : "");
		}
		return list;
	}
	size_t vlen = value_clean_len(value);
	if (vlen == 0) {
		if (!warned_value) {
			warned_value = true;
			log_warn("upload: dropping header `%s` (empty or contains CR/LF)", name);
		}
		return list;
	}
	size_t n = strlen(name) + vlen + 3;
	char *line = malloc(n);
	if (!line) {
		*oom = true;
		return list;
	}
	snprintf(line, n, "%s: %.*s", name, (int)vlen, value);
	struct curl_slist *next = curl_slist_append(list, line);
	free(line);
	if (!next) *oom = true;
	return next ? next : list;
}

int upload_perform(const char *service_name, const char *file_path,
				   struct config *cfg, bool chunked, struct upload_result *out) {
	upload_result_free(out);

	const struct service *svc = gup_find_service(service_name);
	if (!svc) {
		struct sxcu_uploader u = {0};
		if (sxcu_dir_lookup(service_name, &u) == 0) {
			int rc = sxcu_upload(&u, file_path, out);
			sxcu_free(&u);
			return rc;
		}
		log_debug("unknown service: %s", service_name);
		return -1;
	}

	const char *url = svc->url;
	if (!url) {
		url = config_get(cfg, "services.zipline.domain");
		if (!url) {
			log_debug("zipline requires services.zipline.domain");
			return -1;
		}
	}

	const char *auth = gup_resolve_auth(cfg, svc->name);
	if (!auth) return -1;

	if (strcmp(svc->name, "zipline") == 0) {
		const char *cv = config_get(cfg, "services.zipline.chunked");
		if (chunked || (cv && strcmp(cv, "true") == 0))
			return zipline_upload_partial(url, auth, cfg, file_path, out);
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		log_error("curl_easy_init failed");
		return -1;
	}

	curl_mime *mime = curl_mime_init(curl);
	curl_mimepart *part = curl_mime_addpart(mime);
	curl_mime_name(part, "file");
	curl_mime_filedata(part, file_path);

	if (strcmp(svc->name, "nest") == 0) {
		const char *folder = config_get(cfg, "services.nest.folder");
		if (folder) {
			curl_mimepart *fp = curl_mime_addpart(mime);
			curl_mime_name(fp, "folder");
			curl_mime_data(fp, folder, CURL_ZERO_TERMINATED);
		}
	}

	struct curl_slist *headers = NULL;
	bool hdr_oom = false;
	if (svc->auth_in_form) {
		curl_mimepart *ap = curl_mime_addpart(mime);
		curl_mime_name(ap, svc->auth_name);
		curl_mime_data(ap, auth, CURL_ZERO_TERMINATED);
	} else {
		headers = upload_header_append(headers, svc->auth_name, auth, &hdr_oom);
	}

	if (strcmp(svc->name, "zipline") == 0 && cfg) {
		headers = zipline_option_headers(cfg, headers, &hdr_oom);
	}

	if (hdr_oom) {
		log_error("upload: header allocation failed");
		curl_slist_free_all(headers);
		curl_mime_free(mime);
		curl_easy_cleanup(curl);
		return -1;
	}

	struct grabit_buf body = {0};
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, upload_curl_buf_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	upload_curl_common(curl);

	log_info("uploading %s to %s ...", grabit_basename(file_path), svc->name);
	char safe_url[512];
	grabit_redact_url(url, safe_url, sizeof safe_url);
	log_debug("POST %s", safe_url);
	CURLcode rc = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	out->http_code = http_code;
	out->body = body.data;
	body.data = NULL;

	curl_slist_free_all(headers);
	curl_mime_free(mime);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		out->curl_code = (int)rc;
		upload_log_curl_failure((int)rc);
		return -1;
	}
	if (http_code == 413 && strcmp(svc->name, "zipline") == 0) {
		log_info("zipline: upload rejected as too large (HTTP 413); retrying in chunks");
		upload_result_free(out);
		return zipline_upload_partial(url, auth, cfg, file_path, out);
	}
	if (http_code != 200) {
		upload_log_http_failure(http_code, out->body);
		return -1;
	}

	struct json_object *root = out->body
								   ? json_tokener_parse(out->body)
								   : NULL;
	if (!root || json_object_get_type(root) != json_type_object) {
		log_error("invalid JSON response from %s", svc->name);
		upload_log_response_body(out->body);
		if (root) json_object_put(root);
		return -1;
	}

	out->url = extract_url(root, svc->json_path);
	json_object_put(root);

	if (!out->url) {
		log_error("could not find %s in %s response", svc->json_path, svc->name);
		upload_log_response_body(out->body);
		return -1;
	}
	return 0;
}
