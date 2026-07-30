// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ocr/ocr.h"

#include "log.h"
#include "upload/upload.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define LIBRE_MAX_INPUT (64u * 1024u)
#define LIBRE_TIMEOUT_S 30L

static char *build_body(const char *text, const char *target, const char *api_key) {
	struct json_object *req = json_object_new_object();
	if (!req) return NULL;
	json_object_object_add(req, "q", json_object_new_string(text));
	json_object_object_add(req, "source", json_object_new_string("auto"));
	json_object_object_add(req, "target", json_object_new_string(target));
	json_object_object_add(req, "format", json_object_new_string("text"));
	if (api_key && api_key[0])
		json_object_object_add(req, "api_key", json_object_new_string(api_key));

	const char *s = json_object_to_json_string(req);
	char *out = s ? strdup(s) : NULL;
	json_object_put(req);
	return out;
}

static char *parse_reply(const char *body) {
	struct json_object *root = json_tokener_parse(body);
	if (!root) {
		log_error("translate: libretranslate returned unparseable json");
		return NULL;
	}

	struct json_object *err = NULL;
	if (json_object_object_get_ex(root, "error", &err) &&
		json_object_is_type(err, json_type_string)) {
		log_error("translate: libretranslate: %s", json_object_get_string(err));
		json_object_put(root);
		return NULL;
	}

	struct json_object *t = NULL;
	if (!json_object_object_get_ex(root, "translatedText", &t) ||
		!json_object_is_type(t, json_type_string)) {
		log_error("translate: libretranslate response had no translatedText");
		json_object_put(root);
		return NULL;
	}

	const char *s = json_object_get_string(t);
	char *out = strdup(s ? s : "");
	json_object_put(root);
	if (out) grabit_rstrip(out, strlen(out));
	return out;
}

char *grabit_translate_libre(const char *text, const char *target,
							 const char *url, const char *api_key) {
	if (!text || !target || !target[0]) return NULL;
	if (!url || !url[0]) {
		log_error("translate: translate.url is not set");
		log_error("  point it at a libretranslate server, e.g."
				  " grabit set translate.url http://localhost:5000");
		return NULL;
	}
	if (!text[0]) {
		char *empty = malloc(1);
		if (empty) empty[0] = '\0';
		return empty;
	}

	size_t tlen = strlen(text);
	if (tlen > LIBRE_MAX_INPUT) {
		log_error("translate: text is %zu bytes, over the %u-byte cap", tlen, LIBRE_MAX_INPUT);
		return NULL;
	}

	CURL *c = curl_easy_init();
	if (!c) {
		log_error("translate: curl init failed");
		return NULL;
	}

	char *endpoint = NULL, *body = NULL, *result = NULL;
	struct curl_slist *hdrs = NULL;
	struct grabit_buf resp = {0};

	size_t ulen = strlen(url);
	while (ulen > 0 && url[ulen - 1] == '/')
		ulen--;
	static const char PATH[] = "/translate";
	size_t plen = sizeof PATH - 1;
	bool has_path = ulen >= plen && strncmp(url + ulen - plen, PATH, plen) == 0;
	if (grabit_xasprintf(&endpoint, "%.*s%s", (int)ulen, url, has_path ? "" : PATH) != 0) {
		log_error("translate: out of memory");
		goto done;
	}
	body = build_body(text, target, api_key);
	if (!body) {
		log_error("translate: out of memory");
		goto done;
	}

	hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
	if (!hdrs) {
		log_error("translate: out of memory");
		goto done;
	}

	curl_easy_setopt(c, CURLOPT_URL, endpoint);
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, upload_curl_buf_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
	upload_curl_common(c);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, LIBRE_TIMEOUT_S);

	char safe_ep[512];
	grabit_redact_url(endpoint, safe_ep, sizeof safe_ep);
	log_debug("translate: POST %s", safe_ep);
	CURLcode rc = curl_easy_perform(c);
	if (rc != CURLE_OK) {
		char safe[512];
		grabit_redact_url(url, safe, sizeof safe);
		log_error("translate: libretranslate %s: %s", safe, curl_easy_strerror(rc));
		goto done;
	}

	long http_code = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
	if (!resp.data) {
		log_error("translate: libretranslate returned an empty response (http %ld)", http_code);
		goto done;
	}
	resp.data[resp.len] = '\0';

	if (http_code != 200) {
		log_error("translate: libretranslate returned http %ld", http_code);
		if (http_code == 403)
			log_error("  this server wants an api key: grabit set translate.api_key <key>");
		(void)parse_reply(resp.data);
		goto done;
	}
	result = parse_reply(resp.data);

done:
	grabit_buf_free(&resp);
	free(endpoint);
	free(body);
	if (hdrs) curl_slist_free_all(hdrs);
	curl_easy_cleanup(c);
	return result;
}
