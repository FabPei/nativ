/* intro_skip_test.c -- runtime/ui/intro_skip.h (INTRO-01): which press ends the first-start touch card, and its timeout.
 * Before INTRO-01 nothing but a touch, Esc or F10 ended the card, and the game stayed paused under it: a player who
 * pressed Up / Enter / Cross on it saw a game that seemed hung (owner, 2026-11-10, a portable Tearaway).
 * Build: cc -O1 -g -Wall -I ../ui intro_skip_test.c -o /tmp/intro_skip_test && /tmp/intro_skip_test */
#include <stdio.h>
#include "intro_skip.h"

static int fails, checks;
static void check(const char *what, int ok) { checks++; if (!ok) { fails++; printf("FAILED: %s\n", what); } }

int main(void) {
	check("a key (Up, Enter, a letter) ends it", intro_press_skips(INTRO_KEY, INTRO_K_OTHER, 0));
	check("Alt (held to touch with the mouse) doesn't", !intro_press_skips(INTRO_KEY, INTRO_K_ALT, 0));
	check("Ctrl / Shift / GUI (shortcuts) don't", !intro_press_skips(INTRO_KEY, INTRO_K_MODIFIER, 0));
	check("a pad button (Cross, Circle, the D-pad, Start) ends it", intro_press_skips(INTRO_PAD, INTRO_B_OTHER, 0));
	check("a shoulder (held to steer the touch cursor) doesn't", !intro_press_skips(INTRO_PAD, INTRO_B_SHOULDER, 0));
	check("Cross while the cursor is up (the touch itself) doesn't", !intro_press_skips(INTRO_PAD, INTRO_B_OTHER, 1));
	check("not yet shown: no timeout", !intro_timed_out(-1, 100));
	check("9.9 s up: still there", !intro_timed_out(5.0, 14.9));
	check("10 s up: it ends by itself", intro_timed_out(5.0, 15.0));
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
