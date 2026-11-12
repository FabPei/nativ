/*
 * kb_bind.h - the keyboard's bindings (KB-MAP): which key is which Vita control, by default, and the settings key each
 * is saved under ("bind_<action>", an SDL key name; window.c reads them, the in-game menu and the launcher's Controls >
 * Mapping write them). Every control of the Vita has one, as in Vita3K, with Vita3K's default keys: both sticks, the
 * D-pad, the four face buttons, L and R, Start, Select, the PS button, and a PS TV layout's L2, R2, L3 and R3.
 * The rows are in groups (the menus show a heading per group); the settings keys of the bindings that existed before
 * stay, so a player's rebinds keep working.
 * Header-only C, no SDL: runtime/tests/kb_bind_test.c checks it.
 */
#ifndef RECOMP_KB_BIND_H
#define RECOMP_KB_BIND_H

#include <math.h>

enum {
	KB_MOVE_UP, KB_MOVE_DOWN, KB_MOVE_LEFT, KB_MOVE_RIGHT,   /* the left stick */
	KB_LOOK_UP, KB_LOOK_DOWN, KB_LOOK_LEFT, KB_LOOK_RIGHT,   /* the right stick (keys held: they win over the mouse) */
	KB_UP, KB_DOWN, KB_LEFT, KB_RIGHT,                       /* the D-pad */
	KB_CROSS, KB_CIRCLE, KB_SQUARE, KB_TRIANGLE,
	KB_L, KB_R,
	KB_START, KB_SELECT, KB_PS,                              /* PS: the menu, as a controller's Guide button */
	KB_L2, KB_R2, KB_L3, KB_R3,                              /* a PS TV layout's (a handheld game never sees them) */
	KB_N
};
typedef struct { const char *key, *label, *group, *def; } KbDef;   /* label, group: catalog ids (ui_text.c: English and German; MAP-UI) */
static const KbDef kb_defs[KB_N] = {
	{"bind_move_up", "kb.move_up", "grp.lstick", "W"}, {"bind_move_down", "kb.move_down", "grp.lstick", "S"},
	{"bind_move_left", "kb.move_left", "grp.lstick", "A"}, {"bind_move_right", "kb.move_right", "grp.lstick", "D"},
	{"bind_look_up", "kb.look_up", "grp.rstick", "I"}, {"bind_look_down", "kb.look_down", "grp.rstick", "K"},
	{"bind_look_left", "kb.look_left", "grp.rstick", "J"}, {"bind_look_right", "kb.look_right", "grp.rstick", "L"},
	{"bind_up", "vb.up", "grp.dpad", "Up"}, {"bind_down", "vb.down", "grp.dpad", "Down"},
	{"bind_left", "vb.left", "grp.dpad", "Left"}, {"bind_right", "vb.right", "grp.dpad", "Right"},
	{"bind_cross", "vb.cross", "grp.face", "X"}, {"bind_circle", "vb.circle", "grp.face", "C"},
	{"bind_square", "vb.square", "grp.face", "Z"}, {"bind_triangle", "vb.triangle", "grp.face", "V"},
	{"bind_l", "vb.l_mouse", "grp.shoulder", "Q"}, {"bind_r", "vb.r_mouse", "grp.shoulder", "E"},
	{"bind_start", "vb.start", "grp.system", "Return"}, {"bind_select", "vb.select", "grp.system", "Right Shift"},
	{"bind_ps", "vb.ps", "grp.system", "P"},
	{"bind_l2", "vb.l2", "grp.pstv", "U"}, {"bind_r2", "vb.r2", "grp.pstv", "O"},
	{"bind_l3", "vb.l3", "grp.pstv", "F"}, {"bind_r3", "vb.r3", "grp.pstv", "H"},
};
/* a stick from its four keys (1 = held): the bytes a Vita stick reads (0 up / left, 255 down / right, 128 centre), a
 * diagonal at the circle's edge; opposite keys cancel. Returns whether any direction is held. */
static inline int kb_stick(int up, int down, int left, int right, unsigned char *x, unsigned char *y) {
	const int dx = right - left, dy = down - up;
	const float k = dx && dy ? 0.7071f : 1.0f;
	*x = (unsigned char)lroundf(127.5f + dx * k * 127.5f); *y = (unsigned char)lroundf(127.5f + dy * k * 127.5f);
	return dx || dy;
}
#endif
