// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "log.h"
#include "region/region.h"
#include "upload/upload.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *KNOWN_SERVICES[] = {
	"zipline",
	"nest",
	"fakecrime",
	"ez",
	"guns",
	"pixelvault",
	NULL,
};

static const char *VALS_default_action[] = {"upload", "copy", "save", "pin", NULL};
static const char *VALS_filename_preset[] = {"date", "random", "uuid", "timestamp", NULL};
static const char *VALS_edit_color[] = {"red", "yellow", "green", "blue", "black", "white", NULL};
static const char *VALS_format[] = {"png", "jpeg", "webp", NULL};
static const char *VALS_record_format[] = {"mp4", "webm", "gif", NULL};
static const char *VALS_capture_backend[] = {"auto", "wlr", "ext", NULL};
static const char *VALS_position[] = {
	"top-left",
	"top-center",
	"top-right",
	"bottom-left",
	"bottom-center",
	"bottom-right",
	"center",
	NULL,
};
static const char *VALS_x264_preset[] = {
	"ultrafast",
	"superfast",
	"veryfast",
	"faster",
	"fast",
	"medium",
	"slow",
	"slower",
	"veryslow",
	NULL,
};
static const char *VALS_x264_tune[] = {
	"film",
	"animation",
	"grain",
	"stillimage",
	"psnr",
	"ssim",
	"fastdecode",
	"zerolatency",
	NULL,
};
static const char *VALS_pix_fmt[] = {"yuv420p", "yuv422p", "yuv444p", "yuv420p10le", NULL};

static const char *VALS_zl_format[] = {"random", "date", "uuid", "name", "gfycat", NULL};
static const char *VALS_zl_compress[] = {"jpg", "png", "webp", "jxl", NULL};
static const char *VALS_zl_true_only[] = {"true", NULL};

const struct zl_hdr gcfg_zl_headers[] = {
	{"x-zipline-deletes-at", ZL_FREE, NULL},
	{"x-zipline-format", ZL_ENUM, VALS_zl_format},
	{"x-zipline-image-compression-percent", ZL_INT_PCT, NULL},
	{"x-zipline-image-compression-type", ZL_ENUM, VALS_zl_compress},
	{"x-zipline-password", ZL_FREE, NULL},
	{"x-zipline-max-views", ZL_INT, NULL},
	{"x-zipline-no-json", ZL_ENUM, VALS_zl_true_only},
	{"x-zipline-original-name", ZL_ENUM, VALS_zl_true_only},
	{"x-zipline-folder", ZL_FREE, NULL},
	{"x-zipline-filename", ZL_FREE, NULL},
	{"x-zipline-domain", ZL_FREE, NULL},
	{"x-zipline-file-extension", ZL_FREE, NULL},
};
const size_t gcfg_zl_headers_n = sizeof gcfg_zl_headers / sizeof gcfg_zl_headers[0];

const struct zl_hdr *gcfg_zl_find(const char *name) {
	for (size_t i = 0; i < gcfg_zl_headers_n; i++) {
		if (strcmp(gcfg_zl_headers[i].name, name) == 0) return &gcfg_zl_headers[i];
	}
	return NULL;
}

bool cfg_in_list(const char *needle, const char *const *list) {
	for (size_t i = 0; list[i]; i++) {
		if (strcmp(list[i], needle) == 0) return true;
	}
	return false;
}

bool cfg_is_known_service(const char *s) {
	return cfg_in_list(s, KNOWN_SERVICES);
}

static bool valid_service_key(const char *key) {
	if (strncmp(key, "services.", 9) != 0) return false;
	const char *rest = key + 9;
	const char *dot = strchr(rest, '.');
	if (!dot) return false;
	char svc[32];
	size_t svc_len = (size_t)(dot - rest);
	if (svc_len == 0 || svc_len >= sizeof svc) return false;
	memcpy(svc, rest, svc_len);
	svc[svc_len] = '\0';
	if (!cfg_is_known_service(svc)) return false;

	const char *leaf = dot + 1;
	if (strcmp(leaf, "auth") == 0) return true;
	if (strcmp(leaf, "auth_cmd") == 0) return true;
	if (strcmp(leaf, "domain") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "chunked") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "chunk_size") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "folder") == 0) return strcmp(svc, "nest") == 0;
	if (strncmp(leaf, "headers.", 8) == 0) return strcmp(svc, "zipline") == 0 && leaf[8] != '\0';
	return false;
}

static int validate_int_in_range(const char *key, const char *value, long lo, long hi) {
	if (!*value) {
		log_error("%s must be an integer", key);
		return -1;
	}
	char *end = NULL;
	long n = strtol(value, &end, 10);
	if (!end || *end != '\0') {
		log_error("%s must be an integer", key);
		return -1;
	}
	if (n < lo || n > hi) {
		log_error("%s must be between %ld and %ld", key, lo, hi);
		return -1;
	}
	return 0;
}

static int validate_zl_header(const char *hdr, const char *value) {
	const struct zl_hdr *spec = gcfg_zl_find(hdr);
	if (!spec) {
		log_warn("unknown zipline header %s; forwarding as-is", hdr);
		return 0;
	}
	switch (spec->kind) {
	case ZL_FREE:
		return 0;
	case ZL_ENUM:
		if (cfg_in_list(value, spec->allowed)) return 0;
		if (spec->allowed[0] && !spec->allowed[1]) {
			log_error("%s must be \"%s\" (omit the header to disable)", hdr, spec->allowed[0]);
		} else {
			struct grabit_buf b = {0};
			for (size_t i = 0; spec->allowed[i]; i++) {
				if (i) grabit_buf_putc(&b, '|');
				grabit_buf_puts(&b, spec->allowed[i]);
			}
			log_error("%s must be one of %s", hdr, b.data ? b.data : "(none)");
			grabit_buf_free(&b);
		}
		return -1;
	case ZL_INT:
	case ZL_INT_PCT: {
		if (!*value) {
			log_error("%s must be an integer", hdr);
			return -1;
		}
		char *end = NULL;
		long n = strtol(value, &end, 10);
		if (!end || *end != '\0') {
			log_error("%s must be an integer", hdr);
			return -1;
		}
		if (spec->kind == ZL_INT_PCT && (n < 0 || n > 100)) {
			log_error("%s must be between 0 and 100", hdr);
			return -1;
		}
		if (spec->kind == ZL_INT && n < 0) {
			log_error("%s must be a non-negative integer", hdr);
			return -1;
		}
		return 0;
	}
	}
	return -1;
}

static char *normalize_zipline_domain(const char *value) {
	if (!value || !*value) return NULL;
	bool has_scheme = strncmp(value, "http://", 7) == 0 || strncmp(value, "https://", 8) == 0;
	size_t vlen = strlen(value);
	while (vlen > 0 && value[vlen - 1] == '/')
		vlen--;
	const char *suffix = "/api/upload";
	size_t slen = strlen(suffix);
	bool has_path = vlen >= slen && strncmp(value + vlen - slen, suffix, slen) == 0;
	char *out = NULL;
	int rc;
	if (has_scheme && has_path)
		rc = grabit_xasprintf(&out, "%.*s", (int)vlen, value);
	else if (has_scheme)
		rc = grabit_xasprintf(&out, "%.*s/api/upload", (int)vlen, value);
	else if (has_path)
		rc = grabit_xasprintf(&out, "https://%.*s", (int)vlen, value);
	else
		rc = grabit_xasprintf(&out, "https://%.*s/api/upload", (int)vlen, value);
	return rc == 0 ? out : NULL;
}

static int validate_edit_color(const char *value) {
	const char *p = (*value == '#') ? value + 1 : value;
	size_t len = strlen(p);
	bool valid_hex = (len == 6 || len == 3);
	if (valid_hex) {
		for (size_t i = 0; i < len; i++) {
			char hc = p[i];
			if (!((hc >= '0' && hc <= '9') || (hc >= 'a' && hc <= 'f') ||
				  (hc >= 'A' && hc <= 'F'))) {
				valid_hex = false;
				break;
			}
		}
	}
	if (!valid_hex && !cfg_in_list(value, VALS_edit_color)) {
		log_error("edit.color must be #RRGGBB or one of red|yellow|green|blue|black|white");
		return -1;
	}
	return 0;
}

static int validate_service(const char *value) {
	if (upload_service_known(value)) return 0;
	log_error("service `%s` is not a built-in or a registered sxcu uploader", value);
	log_error("  built-ins: zipline|nest|fakecrime|ez|guns|pixelvault");
	log_error("  add a custom one with: grabit sxcu add <file.sxcu>");
	return -1;
}

static const struct cfg_key_desc CFG_KEYS[] = {
	{.key = "default_action", .label = "Default action", .kind = CFG_ENUM, .vals = VALS_default_action, .def = "copy"},
	{.key = "notifications", .label = "Desktop notifications", .kind = CFG_BOOL, .def = "true"},
	{.key = "save_captures", .label = "Always save captures", .kind = CFG_BOOL, .def = "false"},
	{.key = "also_save", .label = "Also keep a local copy", .kind = CFG_BOOL, .def = "false"},
	{.key = "save_dir", .label = "Save folder", .kind = CFG_STRING, .is_path = true, .is_dir = true},
	{.key = "editor", .label = "External editor", .kind = CFG_STRING, .is_path = true},
	{.key = "filename", .label = "Filename template", .kind = CFG_STRING},
	{.key = "filename_preset", .label = "Filename preset", .kind = CFG_ENUM, .vals = VALS_filename_preset},
	{.key = "service", .label = "Upload service", .kind = CFG_STRING, .is_service = true, .vals = KNOWN_SERVICES, .validate = validate_service},
	{.key = "format", .label = "Image format", .kind = CFG_ENUM, .vals = VALS_format, .def = "png"},
	{.key = "recording.fps", .label = "Frame rate (fps)", .kind = CFG_INT, .lo = 1, .hi = 120, .def = "30"},
	{.key = "recording.crf", .label = "Quality (CRF, lower=better)", .kind = CFG_INT, .lo = 0, .hi = 51, .def = "23"},
	{.key = "recording.max_size_mb", .label = "Max size (MB, 0=off)", .kind = CFG_INT, .lo = 0, .hi = 100000, .def = "0"},
	{.key = "recording.cursor", .label = "Record the cursor", .kind = CFG_BOOL, .def = "true"},
	{.key = "recording.ffmpeg", .label = "ffmpeg binary", .kind = CFG_STRING, .def = "ffmpeg", .is_path = true},
	{.key = "recording.preset", .label = "Encoder preset", .kind = CFG_ENUM, .vals = VALS_x264_preset, .def = "fast"},
	{.key = "recording.tune", .label = "Encoder tune", .kind = CFG_ENUM, .vals = VALS_x264_tune, .allow_empty = true},
	{.key = "recording.pix_fmt", .label = "Pixel format", .kind = CFG_ENUM, .vals = VALS_pix_fmt, .def = "yuv420p"},
	{.key = "recording.format", .label = "Recording format", .kind = CFG_ENUM, .vals = VALS_record_format, .def = "mp4"},
	{.key = "ocr.tesseract", .label = "tesseract binary", .kind = CFG_STRING, .def = "tesseract", .is_path = true},
	{.key = "sound.enabled", .label = "Play a shutter sound", .kind = CFG_BOOL, .def = "false"},
	{.key = "sound.player", .label = "Sound player", .kind = CFG_STRING, .is_path = true},
	{.key = "sound.file", .label = "Sound file", .kind = CFG_STRING, .is_path = true},
	{.key = "edit.color", .label = "Annotation color", .kind = CFG_STRING, .validate = validate_edit_color, .def = "red"},
	{.key = "edit.width", .label = "Stroke width", .kind = CFG_INT, .lo = 1, .hi = 20, .def = "4"},
	{.key = "edit.tool", .label = "Default tool", .kind = CFG_ENUM, .vals = grabit_tool_names, .def = "pen"},
	{.key = "edit.default", .label = "Annotate every capture", .kind = CFG_BOOL, .def = "false"},
	{.key = "jpeg.quality", .label = "JPEG quality", .kind = CFG_INT, .lo = 1, .hi = 100, .def = "90"},
	{.key = "webp.quality", .label = "WebP quality", .kind = CFG_INT, .lo = 0, .hi = 100, .def = "85"},
	{.key = "webp.lossless", .label = "WebP lossless", .kind = CFG_BOOL, .def = "false"},
	{.key = "capture.backend", .label = "Capture backend", .kind = CFG_ENUM, .vals = VALS_capture_backend, .def = "auto"},
	{.key = "capture.cursor", .label = "Include the pointer", .kind = CFG_BOOL, .def = "true"},
	{.key = "region.window_snap", .label = "Snap to windows", .kind = CFG_BOOL, .def = "true"},
	{.key = "region.confirm", .label = "Confirm before capture", .kind = CFG_BOOL, .def = "false"},
	{.key = "translate.target", .label = "Translate to (language)", .kind = CFG_STRING},
	{.key = "text_card.dismiss_secs", .label = "OCR card timeout (s)", .kind = CFG_INT, .lo = 0, .hi = 600, .def = "8"},
	{.key = "text_card.position", .label = "OCR card position", .kind = CFG_ENUM, .vals = VALS_position, .def = "bottom-right"},
	{.key = "text_card.output", .label = "OCR card monitor", .kind = CFG_STRING, .is_monitor = true},
	{.key = "preview.enabled", .label = "Show preview card", .kind = CFG_BOOL, .def = "false"},
	{.key = "preview.size", .label = "Preview width (px)", .kind = CFG_INT, .lo = 100, .hi = 800, .def = "300"},
	{.key = "preview.position", .label = "Preview position", .kind = CFG_ENUM, .vals = VALS_position, .def = "bottom-right"},
	{.key = "preview.output", .label = "Preview monitor", .kind = CFG_STRING, .is_monitor = true},
	{.key = "preview.dismiss_secs", .label = "Preview timeout (s)", .kind = CFG_INT, .lo = 0, .hi = 600, .def = "5"},
	{.key = "services.zipline.auth", .label = "Token", .kind = CFG_STRING, .is_secret = true},
	{.key = "services.zipline.auth_cmd", .label = "Token command", .kind = CFG_STRING},
	{.key = "services.zipline.domain", .label = "Domain", .kind = CFG_STRING},
	{.key = "services.zipline.chunked", .label = "Chunked upload", .kind = CFG_BOOL, .def = "false"},
	{.key = "services.zipline.chunk_size", .label = "Chunk size (MB)", .kind = CFG_INT, .lo = 1, .hi = 95, .def = "25"},
	{.key = "services.nest.auth", .label = "Token", .kind = CFG_STRING, .is_secret = true},
	{.key = "services.nest.auth_cmd", .label = "Token command", .kind = CFG_STRING},
	{.key = "services.nest.folder", .label = "Folder", .kind = CFG_STRING},
	{.key = "services.fakecrime.auth", .label = "Token", .kind = CFG_STRING, .is_secret = true},
	{.key = "services.fakecrime.auth_cmd", .label = "Token command", .kind = CFG_STRING},
	{.key = "services.ez.auth", .label = "Token", .kind = CFG_STRING, .is_secret = true},
	{.key = "services.ez.auth_cmd", .label = "Token command", .kind = CFG_STRING},
	{.key = "services.guns.auth", .label = "Token", .kind = CFG_STRING, .is_secret = true},
	{.key = "services.guns.auth_cmd", .label = "Token command", .kind = CFG_STRING},
	{.key = "services.pixelvault.auth", .label = "Token", .kind = CFG_STRING, .is_secret = true},
	{.key = "services.pixelvault.auth_cmd", .label = "Token command", .kind = CFG_STRING},
};
static const size_t CFG_KEYS_N = sizeof CFG_KEYS / sizeof CFG_KEYS[0];

const struct cfg_key_desc *cfg_key_descs(size_t *n_out) {
	if (n_out) *n_out = CFG_KEYS_N;
	return CFG_KEYS;
}

const struct cfg_key_desc *cfg_key_desc_find(const char *key) {
	for (size_t i = 0; i < CFG_KEYS_N; i++) {
		if (strcmp(CFG_KEYS[i].key, key) == 0) return &CFG_KEYS[i];
	}
	return NULL;
}

bool cfg_is_bool_key(const char *key) {
	const struct cfg_key_desc *d = cfg_key_desc_find(key);
	return d && d->kind == CFG_BOOL;
}

bool cfg_key_is_known(const char *key) {
	return cfg_key_desc_find(key) != NULL || valid_service_key(key);
}

static int validate_enum(const char *key, const struct cfg_key_desc *d, const char *value) {
	if (d->allow_empty && !*value) return 0;
	if (cfg_in_list(value, d->vals)) return 0;
	struct grabit_buf b = {0};
	for (size_t i = 0; d->vals[i]; i++) {
		if (i) grabit_buf_putc(&b, '|');
		grabit_buf_puts(&b, d->vals[i]);
	}
	log_error("%s must be one of %s", key, b.data ? b.data : "(none)");
	grabit_buf_free(&b);
	return -1;
}

int config_set(struct config *c, const char *key, const char *value) {
	const struct cfg_key_desc *d = cfg_key_desc_find(key);
	if (!d && !valid_service_key(key)) {
		log_error("unknown config key: %s", key);
		const char *hint = cfg_help_suggest_key(key);
		if (hint) log_info("did you mean: %s ?", hint);
		return -1;
	}

	if (d && d->validate) {
		if (d->validate(value) != 0) return -1;
	} else if (d) {
		switch (d->kind) {
		case CFG_BOOL:
			if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
				log_error("%s must be true or false", key);
				return -1;
			}
			break;
		case CFG_ENUM:
			if (validate_enum(key, d, value) != 0) return -1;
			break;
		case CFG_INT:
			if (validate_int_in_range(key, value, d->lo, d->hi) != 0) return -1;
			break;
		case CFG_STRING:
			break;
		}
	}

	const char *zl_prefix = "services.zipline.headers.";
	if (strncmp(key, zl_prefix, strlen(zl_prefix)) == 0) {
		if (validate_zl_header(key + strlen(zl_prefix), value) != 0) return -1;
	}

	char *normalized = NULL;
	if (strcmp(key, "services.zipline.domain") == 0) {
		normalized = normalize_zipline_domain(value);
		if (!normalized) {
			log_error("out of memory");
			return -1;
		}
		if (strcmp(normalized, value) != 0) {
			log_info("zipline: normalized domain to %s", normalized);
		}
		value = normalized;
	}

	int rc = cfg_kv_upsert(c, key, value);
	free(normalized);
	if (rc != 0) {
		log_error("out of memory");
		return -1;
	}
	return 0;
}
