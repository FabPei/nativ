/*
 * pad_rows.h - the controller mapping as the menus show it (MAP-UI): one row per Vita and PS TV button (PADB_ order, the
 * settings keys window.c reads), its label and group as catalog ids (ui_text.c, English and German), the order the
 * rows are shown in, and each controller button by the pad's own name -- LT on an Xbox pad, L2 on a PlayStation pad, ZL
 * on a Switch pad -- so a row reads "Vita button -> the button on your controller". Shared by the game's menu
 * (overlay.cpp) and the launcher (hub.cpp).
 * Header-only C, no SDL: runtime/tests/pad_rows_test.c checks it.
 */
#ifndef RECOMP_PAD_ROWS_H
#define RECOMP_PAD_ROWS_H

#include <string.h>
#include "pad_bind.h"
#include "pad_family.h"

typedef struct { const char *key, *label, *group, *def; } PadRow;   /* label, group: catalog ids; def: a pad_phys_names entry */
static const PadRow pad_rows[PADB_N] = {
	[PADB_CROSS] = {"pad_bind_cross", "vb.cross", "grp.face", "south"},
	[PADB_CIRCLE] = {"pad_bind_circle", "vb.circle", "grp.face", "east"},
	[PADB_SQUARE] = {"pad_bind_square", "vb.square", "grp.face", "west"},
	[PADB_TRIANGLE] = {"pad_bind_triangle", "vb.triangle", "grp.face", "north"},
	[PADB_START] = {"pad_bind_start", "vb.start", "grp.system", "start"},
	[PADB_SELECT] = {"pad_bind_select", "vb.select", "grp.system", "back"},
	[PADB_UP] = {"pad_bind_up", "vb.up", "grp.dpad", "dpad_up"},
	[PADB_DOWN] = {"pad_bind_down", "vb.down", "grp.dpad", "dpad_down"},
	[PADB_LEFT] = {"pad_bind_left", "vb.left", "grp.dpad", "dpad_left"},
	[PADB_RIGHT] = {"pad_bind_right", "vb.right", "grp.dpad", "dpad_right"},
	[PADB_L3] = {"pad_bind_l3", "vb.l3", "grp.pstv", "left_stick"},
	[PADB_R3] = {"pad_bind_r3", "vb.r3", "grp.pstv", "right_stick"},
	[PADB_L] = {"pad_bind_l", "vb.l", "grp.shoulder", "left_trigger"},
	[PADB_R] = {"pad_bind_r", "vb.r", "grp.shoulder", "right_trigger"},
	[PADB_L2] = {"pad_bind_l2", "vb.l2", "grp.pstv", "left_shoulder"},
	[PADB_R2] = {"pad_bind_r2", "vb.r2", "grp.pstv", "right_shoulder"},
	[PADB_PS] = {"pad_bind_ps", "vb.ps", "grp.system", "guide"},
};
/* the order shown: face buttons, D-pad, shoulders, Start / Select / PS, then the PS TV's own (L2 R2 L3 R3) */
static const int pad_row_order[PADB_N] = {
	PADB_CROSS, PADB_CIRCLE, PADB_SQUARE, PADB_TRIANGLE,
	PADB_UP, PADB_DOWN, PADB_LEFT, PADB_RIGHT,
	PADB_L, PADB_R,
	PADB_START, PADB_SELECT, PADB_PS,
	PADB_L2, PADB_R2, PADB_L3, PADB_R3,
};
/* the controller buttons the mapping uses (pad_bind.c's names) */
enum { PAD_PHYS_N = 17 };
static const char *const pad_phys_names[PAD_PHYS_N] = {"south", "east", "west", "north", "start", "back", "dpad_up", "dpad_down",
	"dpad_left", "dpad_right", "left_stick", "right_stick", "left_shoulder", "right_shoulder", "left_trigger", "right_trigger", "guide"};
/* a controller button by the name printed on the pad of that family; the D-pad's and an unbound row's ("none", "") as
 * catalog ids ("btn.*", to translate). Unknown names come back as they are. */
static inline const char *pad_phys_label(int fam, const char *name) {
	const PadNames *n = pad_names_of(fam);
	const int xb = fam == PAD_FAM_XBOX, sw = fam == PAD_FAM_SWITCH, deck = fam == PAD_FAM_DECK;
	if (!name || !*name || !strcmp(name, "none")) return "btn.none";
	if (!strcmp(name, "south")) return n->select;
	if (!strcmp(name, "east")) return n->back;
	if (!strcmp(name, "west")) return n->west;
	if (!strcmp(name, "north")) return n->north;
	if (!strcmp(name, "start")) return n->menu;
	if (!strcmp(name, "back")) return n->view;
	if (!strcmp(name, "left_shoulder")) return n->l;
	if (!strcmp(name, "right_shoulder")) return n->r;
	if (!strcmp(name, "left_trigger")) return xb ? "LT" : sw ? "ZL" : "L2";
	if (!strcmp(name, "right_trigger")) return xb ? "RT" : sw ? "ZR" : "R2";
	if (!strcmp(name, "left_stick")) return xb || sw ? "LS" : "L3";
	if (!strcmp(name, "right_stick")) return xb || sw ? "RS" : "R3";
	if (!strcmp(name, "guide")) return xb ? "Xbox" : sw ? "Home" : deck ? "Steam" : "PS";
	if (!strcmp(name, "dpad_up")) return "btn.dpad_up";
	if (!strcmp(name, "dpad_down")) return "btn.dpad_down";
	if (!strcmp(name, "dpad_left")) return "btn.dpad_left";
	if (!strcmp(name, "dpad_right")) return "btn.dpad_right";
	return name;
}
#endif
