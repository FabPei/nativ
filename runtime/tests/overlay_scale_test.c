/* ui_scale.h: the in-game menu's scale from the window height and the display's content scale (UI QUEUE U2).
 * Sizes are tuned for 720 lines; a smaller window keeps scale 1 (the menu was laid out for the Vita's 960x544),
 * the display scale lifts that floor (544 lines x scale), at most 4. No SDL:
 *     cc -O1 -g -Wall -I ../ui overlay_scale_test.c -o /tmp/overlay_scale_test -lm && /tmp/overlay_scale_test
 */
#include <math.h>
#include <stdio.h>
#include "ui_scale.h"

static int fails, oks;
static void near(float h, float dpi, float want) {
	float got = overlay_ui_scale(h, dpi);
	if (fabsf(got - want) < 0.002f) oks++;
	else { fails++; printf("FAIL h=%g dpi=%g: %g, want %g\n", h, dpi, got, want); }
}

int main(void) {
	near(544, 1, 1.0f);          /* the Vita's size */
	near(720, 1, 1.0f);          /* 1280x720 */
	near(800, 1, 800.0f / 720);  /* 1280x800, the Steam Deck */
	near(1080, 1, 1.5f);         /* 1920x1080 */
	near(2160, 1, 3.0f);         /* 3840x2160 */
	near(544, 2, 1088.0f / 720); /* a small window on a 200% display */
	near(1080, 1.25f, 1.5f);     /* the window height wins over the floor */
	near(200, 1, 1.0f);          /* tiny: never below 1 */
	near(8000, 1, 4.0f);         /* huge: at most 4 */
	near(720, 0, 1.0f);          /* no display scale known (0): as 1 */
	near(0, 1, 1.0f);            /* no height yet */
	if (fails) { printf("%d failed, %d ok\n", fails, oks); return 1; }
	printf("all passed (%d ok)\n", oks);
	return 0;
}
