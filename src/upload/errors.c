// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

void upload_log_response_body(const char *body) {
	if (!body || !body[0]) return;
	enum { BODY_MAX = 512 };
	size_t len = strlen(body);
	if (len <= BODY_MAX) {
		log_error("response: %s", body);
		return;
	}
	log_error("response (truncated, %zu bytes): %.*s…", len, (int)BODY_MAX, body);
}

static const char *http_friendly(long code) {
	switch (code) {
	case 401:
		return "authentication failed - check your auth token";
	case 403:
		return "forbidden - check permissions on the upload endpoint";
	case 404:
		return "endpoint not found - check the upload URL";
	case 413:
		return "file too large - try compressing";
	case 422:
		return "file rejected (invalid format or validation failure)";
	case 429:
		return "rate limited - try again in a bit";
	case 500:
	case 502:
	case 503:
	case 504:
		return "server error - try again later";
	case 0:
		return "no response from server";
	default:
		return NULL;
	}
}

static const char *curl_friendly(int code) {
	switch (code) {
	case CURLE_COULDNT_RESOLVE_HOST:
		return "couldn't resolve host - check your network";
	case CURLE_COULDNT_CONNECT:
		return "couldn't connect to server";
	case CURLE_OPERATION_TIMEDOUT:
		return "upload timed out";
	case CURLE_RECV_ERROR:
	case CURLE_SEND_ERROR:
		return "connection dropped mid-upload";
	case CURLE_SSL_CONNECT_ERROR:
	case CURLE_PEER_FAILED_VERIFICATION:
		return "TLS connection failed";
	default:
		return NULL;
	}
}

void upload_log_http_failure(long code, const char *body) {
	const char *summary = http_friendly(code);
	if (summary)
		log_error("upload failed (HTTP %ld): %s", code, summary);
	else
		log_error("upload failed (HTTP %ld)", code);
	if (!summary || code == 0) upload_log_response_body(body);
}

void upload_log_curl_failure(int code) {
	const char *friendly = curl_friendly(code);
	if (friendly)
		log_error("upload: %s (%s)", friendly, curl_easy_strerror((CURLcode)code));
	else
		log_error("curl: %s", curl_easy_strerror((CURLcode)code));
}

void upload_friendly_error(const struct upload_result *r, char *out, size_t cap) {
	if (!out || cap == 0) return;
	if (!r) {
		snprintf(out, cap, "upload failed");
		return;
	}
	if (r->curl_code != 0) {
		const char *s = curl_friendly(r->curl_code);
		snprintf(out, cap, "%s", s ? s : curl_easy_strerror((CURLcode)r->curl_code));
		return;
	}
	const char *s = http_friendly(r->http_code);
	if (s)
		snprintf(out, cap, "%s", s);
	else
		snprintf(out, cap, "upload failed (HTTP %ld)", r->http_code);
}

void upload_result_free(struct upload_result *r) {
	if (!r) return;
	free(r->url);
	free(r->body);
	r->url = r->body = NULL;
	r->http_code = 0;
	r->curl_code = 0;
}
