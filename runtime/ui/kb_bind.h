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
typedef struct { const char *key, *label, *group, *def; } KbDef;
static const KbDef kb_defs[KB_N] = {
	{"bind_move_up", "Left stick up", "Left stick", "W"}, {"bind_move_down", "Left stick down", "Left stick", "S"},
	{"bind_move_left", "Left stick left", "Left stick", "A"}, {"bind_move_right", "Left stick right", "Left stick", "D"},
	{"bind_look_up", "Right stick up", "Right stick", "I"}, {"bind_look_down", "Right stick down", "Right stick", "K"},
	{"bind_look_left", "Right stick left", "Right stick", "J"}, {"bind_look_right", "Right stick right", "Right stick", "L"},
	{"bind_up", "D-pad up", "D-pad", "Up"}, {"bind_down", "D-pad down", "D-pad", "Down"},
	{"bind_left", "D-pad left", "D-pad", "Left"}, {"bind_right", "D-pad right", "D-pad", "Right"},
	{"bind_cross", "Cross", "Face buttons", "X"}, {"bind_circle", "Circle", "Face buttons", "C"},
	{"bind_square", "Square", "Face buttons", "Z"}, {"bind_triangle", "Triangle", "Face buttons", "V"},
	{"bind_l", "L (also the right mouse button)", "Shoulders", "Q"}, {"bind_r", "R (also the left mouse button)", "Shoulders", "E"},
	{"bind_start", "Start", "System", "Return"}, {"bind_select", "Select", "System", "Right Shift"},
	{"bind_ps", "PS button (the menu)", "System", "P"},
	{"bind_l2", "L2", "PS TV layout", "U"}, {"bind_r2", "R2", "PS TV layout", "O"},
	{"bind_l3", "L3", "PS TV layout", "F"}, {"bind_r3", "R3", "PS TV layout", "H"},
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
