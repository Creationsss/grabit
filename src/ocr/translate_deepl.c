// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ocr/ocr.h"

#include "log.h"
#include "upload/upload.h"
#include "util.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define DEEPL_HOST_FREE "https://api-free.deepl.com"
#define DEEPL_HOST_PRO "https://api.deepl.com"
#define DEEPL_MAX_INPUT (64u * 1024u)
#define DEEPL_TIMEOUT_S 30L

static bool key_is_free(const char *key) {
	size_t n = strlen(key);
	return n > 3 && strcmp(key + n - 3, ":fx") == 0;
}

static char *parse_reply(const char *body) {
	struct json_object *root = json_tokener_parse(body);
	if (!root) {
		log_error("translate: deepl returned unparseable json");
		return NULL;
	}

	struct json_object *arr = NULL;
	if (!json_object_object_get_ex(root, "translations", &arr) ||
		!json_object_is_type(arr, json_type_array) ||
		json_object_array_length(arr) == 0) {
		log_error("translate: deepl response had no translations");
		json_object_put(root);
		return NULL;
	}

	struct grabit_buf out = {0};
	size_t n = json_object_array_length(arr);
	for (size_t i = 0; i < n; i++) {
		struct json_object *el = json_object_array_get_idx(arr, i);
		struct json_object *t = NULL;
		if (!el || !json_object_object_get_ex(el, "text", &t) ||
			!json_object_is_type(t, json_type_string))
			continue;
		const char *s = json_object_get_string(t);
		if (s && s[0] && grabit_buf_puts(&out, s) != 0) {
			grabit_buf_free(&out);
			json_object_put(root);
			log_error("translate: out of memory");
			return NULL;
		}
	}
	json_object_put(root);

	if (!out.data) {
		char *empty = malloc(1);
		if (empty) empty[0] = '\0';
		return empty;
	}
	out.data[out.len] = '\0';
	grabit_rstrip(out.data, strlen(out.data));
	return out.data;
}

static void log_http_error(long code, const char *body) {
	switch (code) {
	case 403:
		log_error("translate: deepl rejected the key (http 403)");
		log_error("  check GRABIT_TRANSLATE_KEY / translate.api_key, and that the key"
				  " matches the endpoint (free keys end in `:fx`)");
		break;
	case 429:
	case 529:
		log_error("translate: deepl is rate limiting (http %ld); retry shortly", code);
		break;
	case 456:
		log_error("translate: deepl character quota exhausted for this billing period"
				  " (http 456)");
		break;
	case 400:
		log_error("translate: deepl rejected the request (http 400);"
				  " check translate.target is a code deepl supports, e.g. EN-US, PT-BR, ZH-HANS");
		break;
	default:
		log_error("translate: deepl returned http %ld", code);
		break;
	}
	if (body && body[0]) log_error("  server said: %.200s", body);
}

char *grabit_translate_deepl(const char *text, const char *target,
							 const char *url, const char *api_key) {
	if (!text || !target || !target[0]) return NULL;
	if (!api_key || !api_key[0]) {
		log_error("translate: deepl needs an api key");
		log_error("  set GRABIT_TRANSLATE_KEY, or: grabit set translate.api_key <key>");
		return NULL;
	}
	if (!text[0]) {
		char *empty = malloc(1);
		if (empty) empty[0] = '\0';
		return empty;
	}

	size_t tlen = strlen(text);
	if (tlen > DEEPL_MAX_INPUT) {
		log_error("translate: text is %zu bytes, over the %u-byte cap", tlen, DEEPL_MAX_INPUT);
		return NULL;
	}

	CURL *c = curl_easy_init();
	if (!c) {
		log_error("translate: curl init failed");
		return NULL;
	}

	char *endpoint = NULL, *body = NULL, *auth = NULL, *upper = NULL, *result = NULL;
	struct curl_slist *hdrs = NULL;
	struct grabit_buf resp = {0};
	char *enc_text = curl_easy_escape(c, text, (int)tlen);
	char *enc_lang = NULL;

	upper = strdup(target);
	if (!upper || !enc_text) {
		log_error("translate: out of memory");
		goto done;
	}
	for (char *p = upper; *p; p++) *p = (char)toupper((unsigned char)*p);
	enc_lang = curl_easy_escape(c, upper, 0);
	if (!enc_lang) {
		log_error("translate: out of memory");
		goto done;
	}

	if (url && url[0]) {
		size_t ulen = strlen(url);
		while (ulen > 0 && url[ulen - 1] == '/') ulen--;
		static const char PATH[] = "/v2/translate";
		size_t plen = sizeof PATH - 1;
		bool has_path = ulen >= plen && strncmp(url + ulen - plen, PATH, plen) == 0;
		if (grabit_xasprintf(&endpoint, "%.*s%s", (int)ulen, url, has_path ? "" : PATH) != 0)
			goto oom;
	} else if (grabit_xasprintf(&endpoint, "%s/v2/translate",
								key_is_free(api_key) ? DEEPL_HOST_FREE : DEEPL_HOST_PRO) != 0) {
		goto oom;
	}

	if (grabit_xasprintf(&body, "text=%s&target_lang=%s", enc_text, enc_lang) != 0) goto oom;
	if (grabit_xasprintf(&auth, "Authorization: DeepL-Auth-Key %s", api_key) != 0) goto oom;

	hdrs = curl_slist_append(hdrs, auth);
	if (!hdrs) goto oom;

	curl_easy_setopt(c, CURLOPT_URL, endpoint);
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, upload_curl_buf_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
	upload_curl_common(c);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, DEEPL_TIMEOUT_S);

	log_debug("translate: POST %s", endpoint);
	CURLcode rc = curl_easy_perform(c);
	if (rc != CURLE_OK) {
		log_error("translate: deepl request failed: %s", curl_easy_strerror(rc));
		goto done;
	}

	long http_code = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
	if (resp.data) resp.data[resp.len] = '\0';
	if (http_code != 200) {
		log_http_error(http_code, resp.data);
		goto done;
	}
	if (!resp.data) {
		log_error("translate: deepl returned an empty response");
		goto done;
	}
	result = parse_reply(resp.data);
	goto done;

oom:
	log_error("translate: out of memory");

done:
	grabit_buf_free(&resp);
	free(endpoint);
	free(body);
	free(auth);
	free(upper);
	if (enc_text) curl_free(enc_text);
	if (enc_lang) curl_free(enc_lang);
	if (hdrs) curl_slist_free_all(hdrs);
	curl_easy_cleanup(c);
	return result;
}
