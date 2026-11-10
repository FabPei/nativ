/*
 * menu_hint.h - how to open the menu, one line per way (UI-02a, HINT-01): the hint a game shows at its start (top right),
 * the first-start touch card, and the short note when a controller is connected.
 * HINT-01 (owner 2026-11-10: "es soll auch sagen, wie Controller- oder Touchscreennutzer das Menü öffnen"): every way is
 * named, always -- the keyboard's F10 and the mouse at the top edge, a controller's own button pair (pad_family.h: Start +
 * Select, Options + Share, Menu + View, + and -; with none connected the common names), a touch screen's tab at the top
 * centre -- the ways this player has now first. (UI-02a named only the devices found at the start: a player without a
 * pad then never learned the controller's way.)
 * The [..] parts are drawn as key caps by the toast. Plain C, header-only: runtime/tests/menu_hint_test.c checks it.
 */
#ifndef RECOMP_MENU_HINT_H
#define RECOMP_MENU_HINT_H
#include <stdio.h>
#include <string.h>
#include "pad_family.h"

/* the controller's line: pad_type the SDL gamepad type (-1: none connected -- the common names), deck: a Steam Deck */
static inline void menu_hint_pad_line(char *out, size_t n, int pad_type, int deck) {
	if (pad_type >= 0 || deck) {
		const PadNames *p = pad_names_of(pad_family(pad_type, deck));
		snprintf(out, n, "Controller: [%s] + [%s]", p->menu, p->view);
	} else snprintf(out, n, "Controller: [Start] + [Select], or [Menu] + [View]");
}

/* out: the hint's body (lines split by '\n'); keyboard / mouse: there now; pad_type: the player's controller's SDL gamepad
 * type, -1 for none; deck: Steam says this is a Steam Deck; touch: a touch screen is there; osb: the on-screen buttons can
 * come up by a touch (their setting isn't off and no controller is connected) -- then a line says where they are switched
 * off. Every way is named; those there now first, in the order keyboard, controller, touch. Returns the number of lines. */
static inline int menu_hint_text(char *out, size_t n, int keyboard, int mouse, int pad_type, int deck, int touch, int osb) {
	int lines = 0; size_t at = 0;
	char kb[64], pad[96];
	const char *const tl = "Touch: the tab at the top, then Game > Full menu";
	out[0] = 0;
#define MH_LINE(...) do { int w = snprintf(out + at, at < n ? n - at : 0, "%s", lines ? "\n" : ""); at += w > 0 ? (size_t)w : 0; \
	w = snprintf(out + at, at < n ? n - at : 0, __VA_ARGS__); at += w > 0 ? (size_t)w : 0; lines++; } while (0)
	if (keyboard && !mouse) snprintf(kb, sizeof kb, "Keyboard: [F10]");
	else if (mouse && !keyboard) snprintf(kb, sizeof kb, "Mouse: move it to the top edge, or [F10]");
	else snprintf(kb, sizeof kb, "Keyboard: [F10], or the mouse at the top");
	menu_hint_pad_line(pad, sizeof pad, pad_type, deck);
	const int have[3] = {keyboard || mouse, pad_type >= 0 || deck, touch};
	const char *const text[3] = {kb, pad, tl};
	for (int pass = 1; pass >= 0; pass--)   /* (the ways there now, then the others) */
		for (int i = 0; i < 3; i++) if (!!have[i] == pass) MH_LINE("%s", text[i]);
	if (touch && osb) MH_LINE("On-screen buttons: off in the menu, Controls");
#undef MH_LINE
	if (at >= n && n) out[n - 1] = 0;
	return lines;
}
#endif
