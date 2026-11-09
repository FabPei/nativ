/*
 * menu_hint.h - the hint a game shows once at its start (top right): how to open the menu, one line per way the player has
 * (UI-02a). Only what is there is named: the keyboard's F10 and the mouse at the top edge, the controller's own button
 * pair (pad_family.h: Start + Select, Menu + View, + and -), a touch screen's pull tab at the top centre.
 * The [..] parts are drawn as key caps by the toast. Plain C, header-only: runtime/tests/menu_hint_test.c checks it.
 */
#ifndef RECOMP_MENU_HINT_H
#define RECOMP_MENU_HINT_H
#include <stdio.h>
#include <string.h>
#include "pad_family.h"

/* out: the toast's body (lines split by '\n'); pad_type: the player's controller's SDL gamepad type, -1 for none; deck:
 * Steam says this is a Steam Deck; touch: a touch screen is there. Returns the number of lines. */
static inline int menu_hint_text(char *out, size_t n, int keyboard, int mouse, int pad_type, int deck, int touch) {
	int lines = 0; size_t at = 0;
	out[0] = 0;
#define MH_LINE(...) do { int w = snprintf(out + at, at < n ? n - at : 0, "%s", lines ? "\n" : ""); at += w > 0 ? (size_t)w : 0; \
	w = snprintf(out + at, at < n ? n - at : 0, __VA_ARGS__); at += w > 0 ? (size_t)w : 0; lines++; } while (0)
	if (keyboard && mouse) MH_LINE("Keyboard: [F10], or the mouse at the top");
	else if (keyboard) MH_LINE("Keyboard: [F10]");
	else if (mouse) MH_LINE("Mouse: move it to the top edge");
	if (pad_type >= 0 || deck) {
		const PadNames *p = pad_names_of(pad_family(pad_type, deck));
		MH_LINE("Controller: [%s] + [%s]", p->menu, p->view);
	}
	if (touch) MH_LINE("Touch: the tab at the top, then Game > Full menu");
	if (!lines) MH_LINE("[F10] opens the menu");
#undef MH_LINE
	if (at >= n && n) out[n - 1] = 0;
	return lines;
}
#endif
