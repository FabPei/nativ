/* viewport_disabled_test.c -- VPDIS-01: a draw with SCE_GXM_VIEWPORT_DISABLED whose positions are target pixels lands on
 * those pixels on the host GPU, inside the clip volume (gfx/viewport_map.h). Fail-before: gfx_gpu.c gave such a draw the
 * viewport (-1,-1,2,2) and the raw position -- a 960x544 quad's corners at clip x = 960 > w = 1, outside the volume.
 *   cc -O1 -Wall -I ../gfx viewport_disabled_test.c -o ~/viewport_disabled_test -lm && ~/viewport_disabled_test
 */
#include <math.h>
#include <stdio.h>
#include "viewport_map.h"
static int fails;
static void check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
int main(void) {
	const float W = 960, H = 544, px[4][2] = {{0, 0}, {960, 0}, {0, 544}, {960, 544}};
	float ax, ay; vp_disabled_factors(W, H, &ax, &ay);
	int inside = 1, back = 1;
	for (int k = 0; k < 4; k++) for (int wi = 0; wi < 2; wi++) {
		float w = wi ? 2.0f : 1.0f, x = px[k][0] * w, y = px[k][1] * w, cx, cy, dx, dy;   /* (a homogeneous w != 1 too) */
		vp_disabled_clip(x, y, w, ax, ay, &cx, &cy);
		inside &= fabsf(cx) <= w * 1.0001f && fabsf(cy) <= w * 1.0001f;
		vp_device(cx, cy, w, 0, 0, W, H, &dx, &dy);
		back &= fabsf(dx - px[k][0]) < 1e-3f && fabsf(dy - px[k][1]) < 1e-3f;
	}
	check("disabled: the full-target quad's corners are inside the clip volume", inside);
	check("disabled: on the whole-target viewport each corner lands on its own pixel", back);
	{ float cx, cy, dx, dy; vp_disabled_clip(480, 272, 1, ax, ay, &cx, &cy); vp_device(cx, cy, 1, 0, 0, W, H, &dx, &dy);
	  check("disabled: the centre pixel (480,272) maps to NDC (0,0) and back", fabsf(cx) < 1e-6f && fabsf(cy) < 1e-6f && fabsf(dx - 480) < 1e-3f && fabsf(dy - 272) < 1e-3f); }
	{ float cx, cy; vp_disabled_clip(0.25f, -0.5f, 1, 0, 0, &cx, &cy);
	  check("enabled (factors 0): the position is left as it is", cx == 0.25f && cy == -0.5f); }
	{ float cx, cy; vp_disabled_clip(960, 544, 1, 0, 0, &cx, &cy);
	  check("before (no mapping): the quad corner 960 lies outside |x| <= w (the black screen)", fabsf(cx) > 1); }
	printf("viewport_disabled_test: %s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
