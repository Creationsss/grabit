// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/screencast.h"

#include "log.h"
#include "record/pw.h"
#include "record/sc_backend.h"
#include "util/util.h"

#include <stdlib.h>

enum sc_kind {
	SC_UNSET = -1,
	SC_NONE = 0,
	SC_KDE,
	SC_GNOME,
};

struct screencast {
	enum sc_kind kind;
	struct sc_kde *kde;
	struct sc_gnome *gnome;
};

static enum sc_kind sc_resolve(struct grabit_wl_state *s) {
	static enum sc_kind cached = SC_UNSET;
	if (cached != SC_UNSET) return cached;
	if (!pw_available())
		cached = SC_NONE;
	else if (sc_kde_available(s))
		cached = SC_KDE;
	else if (sc_gnome_available())
		cached = SC_GNOME;
	else
		cached = SC_NONE;
	return cached;
}

bool screencast_available(struct grabit_wl_state *s) {
	return sc_resolve(s) != SC_NONE;
}

const char *screencast_backend_name(struct grabit_wl_state *s) {
	switch (sc_resolve(s)) {
	case SC_KDE:
		return "kwin";
	case SC_GNOME:
		return "mutter";
	default:
		return "none";
	}
}

struct screencast *screencast_start(struct grabit_wl_state *s, struct rect r,
									bool cursor, uint32_t *out_node_id) {
	struct screencast *sc = calloc(1, sizeof *sc);
	if (!sc) return NULL;
	sc->kind = sc_resolve(s);

	switch (sc->kind) {
	case SC_KDE:
		sc->kde = sc_kde_start(s, r, cursor, out_node_id);
		if (sc->kde) return sc;
		break;
	case SC_GNOME:
		sc->gnome = sc_gnome_start(r, cursor, out_node_id);
		if (sc->gnome) return sc;
		break;
	default:
		break;
	}

	free(sc);
	return NULL;
}

void screencast_stop(struct screencast *sc) {
	if (!sc) return;
	switch (sc->kind) {
	case SC_KDE:
		sc_kde_stop(sc->kde);
		break;
	case SC_GNOME:
		sc_gnome_stop(sc->gnome);
		break;
	default:
		break;
	}
	free(sc);
}

void screencast_explain_unavailable(void) {
	log_error("  no screencast source either (wanted zkde_screencast_unstable_v1 "
			  "or org.gnome.Mutter.ScreenCast)");
	if (!pw_available())
		log_error("  this grabit was built without pipewire, which both need");
	else if (grabit_desktop_is("KDE"))
		log_error("  on KDE, KWin only offers zkde_screencast_unstable_v1 to an "
				  "installed grabit; run `make install` and use that binary");
}
