// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include <png.h>

int main(void) {
	png_set_cICP(0, 0, 0, 0, 0, 0);
	png_set_mDCV(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	png_set_cLLI(0, 0, 0, 0);
	return 0;
}
