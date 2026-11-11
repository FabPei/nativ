/* target_scale_test.c -- RECOMP_TARGET_SCALE's rules (runtime/gfx/target_scale.h), as gfx_gpu.c applies them per target:
 *  - "720x408:1.3333" draws Uncharted's 720x408 world at 4/3 the render scale (5x at 4x), and only with full_res_world on;
 *  - "360x204:0" (UC-LAMP) keeps that size at the game's own resolution at any render scale, full_res_world or not, and a
 *    wider aspect still widens it; other sizes take the render scale.
 * Fail-before: the old parser took factors 1..4 only and stopped at "360x204:0", so every glow buffer drew at the render
 * scale (Uncharted's lamps: a grid of squares at 4x). Build: README.md here.
 */
#include <stdio.h>
#include "../gfx/target_scale.h"

static int fails;
static void check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	TargetScaleRule r[TARGET_SCALE_RULES]; uint8_t sx, sy;
	int n = target_scale_parse("720x408:1.3333,360x204:0,180x102:0,256x512:0,32x32:0", r);
	check("Uncharted's five rules parse (the 0 factors too)", n == 5 && r[1].f == 0.0f && r[4].w == 32);
	target_scale_of(r, n, 720, 408, 4, 4, 1, &sx, &sy);
	check("720x408 at render scale 4: 5x (4/3 of it, rounded)", sx == 5 && sy == 5);
	target_scale_of(r, n, 720, 408, 4, 4, 0, &sx, &sy);
	check("720x408 with full_res_world off: the render scale (4x)", sx == 4 && sy == 4);
	target_scale_of(r, n, 360, 204, 4, 4, 1, &sx, &sy);
	check("360x204 (factor 0) at render scale 4: 1x", sx == 1 && sy == 1);
	target_scale_of(r, n, 180, 102, 6, 6, 0, &sx, &sy);
	check("180x102 (factor 0) at 6x, full_res_world off: still 1x", sx == 1 && sy == 1);
	target_scale_of(r, n, 256, 512, 4, 8, 1, &sx, &sy);
	check("256x512 (factor 0) at 4x with a 2x wider aspect (8x across): 2x across, 1x down", sx == 2 && sy == 1);
	target_scale_of(r, n, 960, 544, 4, 4, 1, &sx, &sy);
	check("960x544 (no rule): the render scale", sx == 4 && sy == 4);
	check("the rule that decided: 1 for 360x204, -1 for none", target_scale_of(r, n, 360, 204, 4, 4, 1, &sx, &sy) == 1 && target_scale_of(r, n, 960, 544, 4, 4, 1, &sx, &sy) == -1);
	check("a factor above 4 or between 0 and 1 is malformed: parsing stops there", target_scale_parse("720x408:5,360x204:0", r) == 0 && target_scale_parse("360x204:0.5", r) == 0);
	check("no setting: no rules", target_scale_parse(NULL, r) == 0 && target_scale_parse("", r) == 0);
	target_scale_of(r, 0, 720, 408, 8, 8, 1, &sx, &sy);
	check("at most 8 a guest pixel", sx == 8 && sy == 8);
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
