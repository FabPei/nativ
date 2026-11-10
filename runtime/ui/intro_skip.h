/*
 * intro_skip.h - when the first-start touch card ("How to touch the screen", overlay.cpp's build_intro) ends without a
 * touch (INTRO-01, owner 2026-11-10: a new player pressed Up on it, nothing happened, and the paused game looked hung):
 *   - a key press ends it, except Alt (held for a touch with the mouse) and the modifiers that start shortcuts;
 *   - a controller button ends it, except the shoulder buttons (held to steer the touch cursor) and anything pressed
 *     while the cursor is up (Cross then is the touch itself);
 *   - after INTRO_SKIP_S seconds it ends by itself.
 * A touch (the mouse, a finger, the cursor + Cross) still ends it as before, with a moment to see it worked.
 * Header-only C, no SDL: runtime/tests/intro_skip_test.c checks it.
 */
#ifndef RECOMP_INTRO_SKIP_H
#define RECOMP_INTRO_SKIP_H

#define INTRO_SKIP_S 10.0

enum { INTRO_KEY = 0, INTRO_PAD = 1 };
enum { INTRO_K_OTHER = 0, INTRO_K_ALT, INTRO_K_MODIFIER };              /* a key: Alt, Ctrl / Shift / GUI, or any other */
enum { INTRO_B_OTHER = 0, INTRO_B_SHOULDER };                            /* a pad button: a shoulder (L1 / R1, LB / RB), or any other */

/* does this press end the card? kind: INTRO_KEY or INTRO_PAD; what: its INTRO_K_* / INTRO_B_*; cursor_up: the touch cursor is
 * held up (a shoulder held) */
static inline int intro_press_skips(int kind, int what, int cursor_up) {
	if (kind == INTRO_KEY) return what == INTRO_K_OTHER;
	return !cursor_up && what != INTRO_B_SHOULDER;
}
/* has the card been up long enough to end by itself? since: when it first showed (seconds, < 0: not yet) */
static inline int intro_timed_out(double since, double now) { return since >= 0 && now - since >= INTRO_SKIP_S; }
#endif
