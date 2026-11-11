/*
 * A render target's own scale (RECOMP_TARGET_SCALE, a game's RUN_ENV: "<w>x<h>:<factor>[,...]"), as gfx_gpu.c applies it:
 * pure functions, so runtime/tests/target_scale_test.c checks them without a GPU.
 *  - <factor> 1..4: that guest size draws at <factor> times the render scale (rounded; never below the render scale) --
 *    a world drawn below the screen's resolution (Uncharted's 720x408) at the screen's texel density; only while the
 *    "full_res_world" setting is on.
 *  - <factor> 0 (UC-LAMP): that size stays at the game's own resolution whatever the render scale (a wider aspect still
 *    widens it) -- a post-effect chain whose shaders step one guest texel at a time: Uncharted's glow (360x204, 180x102,
 *    its 256x512 level atlas, a 32x32 buffer) downsampled with taps one guest texel apart caught a quarter of each block
 *    at render scale 4, and the lamps' glow came out as a grid of squares (Vita3K, at the game's resolution: smooth).
 * Each target: 8 at most a guest pixel (a 2048-wide target within Metal's 16384 texels).
 */
#ifndef TARGET_SCALE_H
#define TARGET_SCALE_H
#include <stdint.h>
#include <stdio.h>

typedef struct { uint32_t w, h; float f; } TargetScaleRule;
#define TARGET_SCALE_RULES 8

/* parses the setting into rules (at most TARGET_SCALE_RULES); stops at the first malformed entry; returns the count */
static inline int target_scale_parse(const char *e, TargetScaleRule *r) {
	int n = 0;
	while (e && *e && n < TARGET_SCALE_RULES) {
		unsigned w, h; float f; int used = 0;
		if (sscanf(e, "%ux%u:%f%n", &w, &h, &f, &used) == 3 && used > 0 && (f == 0.0f || (f >= 1.0f && f <= 4.0f))) { r[n].w = w; r[n].h = h; r[n].f = f; n++; }
		else break;
		e += used; if (*e == ',') e++;
	}
	return n;
}
/* the scale (sx, sy) of a w x h target at render scale rs (rx horizontally, a wider aspect's); full_res: the setting.
 * Returns the rule that decided it, -1 for none */
static inline int target_scale_of(const TargetScaleRule *r, int n, uint32_t w, uint32_t h, int rs, int rx, int full_res, uint8_t *sx, uint8_t *sy) {
	int x = rx, y = rs, hit = -1;
	for (int k = 0; k < n; k++) if (r[k].f == 0.0f && r[k].w == w && r[k].h == h) { x = (rx + rs - 1) / rs; y = 1; hit = k; break; }
	if (hit < 0 && full_res) for (int k = 0; k < n; k++) if (r[k].f > 0.0f && r[k].w == w && r[k].h == h) {
		int a = (int)((float)rx * r[k].f + 0.5f), b = (int)((float)rs * r[k].f + 0.5f);
		if (a > x) x = a; if (b > y) y = b; hit = k; break;
	}
	*sx = (uint8_t)(x > 8 ? 8 : x); *sy = (uint8_t)(y > 8 ? 8 : y);
	return hit;
}
#endif
