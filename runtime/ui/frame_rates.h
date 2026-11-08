/*
 * The frame-rate rules, one copy for the display (runtime/gfx/display.c), the in-game menu (overlay.cpp) and the
 * launcher (launcher/hub.cpp). Header-only C, no state.
 *
 * - The rates a game allows: its pack's RUN_ENV `RECOMP_FRAME_RATES` (e.g. "30,60,120"; commas or spaces). Absent or
 *   empty: 30 and 60, what every game offered before the list existed. 30 -- the Vita's own -- is always allowed.
 * - The rate a game runs at: the highest allowed rate at or below the `frame_rate` setting (a shared 120 in a game
 *   that allows 30 and 60 runs at 60; a 60 in a game that allows only 30 runs at 30).
 * - 60 and 120 cap the swap interval a game asks WaitSetFrameBufMulti for at one vblank; at 120 the display's vblank
 *   grid runs at 120 Hz (its period 8333 us instead of the hardware's 16667), so a game that presents every vblank
 *   presents 120 times a second. Everything that counts vblanks then counts twice as many: a game timing itself in
 *   vblanks or fixed per-frame steps runs fast unless its pack hooks those steps -- why the rate is per game.
 * - A frame's time for those hooks (hooks.c frame_dt): the vblanks it took over the grid's rate, from one edge up to a
 *   30 fps frame (1/120 .. 1/30 s at 120, 1/60 .. 1/30 s otherwise).
 */
#ifndef FRAME_RATES_H
#define FRAME_RATES_H
#include <stdint.h>

enum { FR_30 = 1, FR_60 = 2, FR_120 = 4 };

static inline int fr_bit(int rate) { return rate >= 120 ? FR_120 : rate >= 60 ? FR_60 : FR_30; }
static inline int fr_rate(int bit) { return bit == FR_120 ? 120 : bit == FR_60 ? 60 : 30; }

/* the allowed rates as a mask of FR_*: the numbers 30, 60 and 120 in the list; anything else is ignored */
static inline int fr_allowed(const char *list) {
	if (!list) return FR_30 | FR_60;
	int m = 0, any = 0;
	for (const char *p = list; *p; ) {
		while (*p == ',' || *p == ' ' || *p == '\t') p++;
		if (!*p) break;
		long v = 0; int digits = 0;
		while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; digits++; }
		if (digits) { any = 1; if (v == 30) m |= FR_30; else if (v == 60) m |= FR_60; else if (v == 120) m |= FR_120; }
		while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
	}
	if (!any) return FR_30 | FR_60;
	return m | FR_30;
}
/* the rate a game runs at for a `frame_rate` setting */
static inline int fr_effective(int setting, int allowed) {
	if (setting >= 120 && (allowed & FR_120)) return 120;
	if (setting >= 60 && (allowed & FR_60)) return 60;
	return 30;
}
/* the allowed rates, lowest first, into out[0..2]; returns how many (1..3) */
static inline int fr_list(int allowed, int out[3]) {
	int n = 0; out[n++] = 30;
	if (allowed & FR_60) out[n++] = 60;
	if (allowed & FR_120) out[n++] = 120;
	return n;
}
/* the vblank grid at a rate: its edges a second and their period in microseconds */
static inline int fr_grid_hz(int rate) { return rate >= 120 ? 120 : 60; }
static inline uint64_t fr_grid_period_us(int rate) { return rate >= 120 ? 8333 : 16667; }
/* WaitSetFrameBufMulti(n)'s interval at a rate: 1..8 as asked at 30, one edge above */
static inline uint32_t fr_swap_interval(uint32_t n, int rate) { if (!n) n = 1; if (n > 8) n = 8; return n > 1 && rate >= 60 ? 1 : n; }
/* a frame's time in seconds from the grid edges it took */
static inline float fr_frame_dt(uint64_t edges, int rate) {
	const uint64_t hz = (uint64_t)fr_grid_hz(rate), most = hz / 30;
	return (float)(edges < 1 ? 1 : edges > most ? most : edges) / (float)hz;
}
#endif
