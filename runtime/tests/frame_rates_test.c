/*
 * frame_rates_test.c -- runtime/ui/frame_rates.h, the frame-rate rules the display, the in-game menu and the launcher
 * share: the pack's allowed rates (RECOMP_FRAME_RATES), the rate a setting gives, the vblank grid at 120, the swap
 * interval and a frame's time for the frame-time hooks.
 *
 * -DFRAME_RATES_LEGACY builds the same checks against the rules display.c had before the header (transcribed: a 30/60
 * switch, `frame_rate >= 60`; a fixed 16667 us grid; no allowed list) -- the fail-before.
 *
 *   cc -O1 -Wall -I ../ui frame_rates_test.c -o /tmp/frame_rates_test && /tmp/frame_rates_test
 *   cc -O1 -Wall -I ../ui -DFRAME_RATES_LEGACY frame_rates_test.c -o /tmp/frame_rates_legacy && /tmp/frame_rates_legacy
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#ifndef FRAME_RATES_LEGACY
#include "frame_rates.h"
#else
enum { FR_30 = 1, FR_60 = 2, FR_120 = 4 };
static int fr_allowed(const char *list) { (void)list; return FR_30 | FR_60; }
static int fr_effective(int setting, int allowed) { (void)allowed; return setting >= 60 ? 60 : 30; }
static int fr_list(int allowed, int out[3]) { (void)allowed; out[0] = 30; out[1] = 60; return 2; }
static int fr_grid_hz(int rate) { (void)rate; return 60; }
static uint64_t fr_grid_period_us(int rate) { (void)rate; return 16667; }
static uint32_t fr_swap_interval(uint32_t n, int rate) { if (!n) n = 1; if (n > 8) n = 8; return n > 1 && rate >= 60 ? 1 : n; }
static float fr_frame_dt(uint64_t e, int rate) { (void)rate; return (float)(e < 1 ? 1 : e > 2 ? 2 : e) / 60.0f; }
#endif

static int checks, failed;
#define CHECK(c) do { checks++; if (!(c)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static int near(float a, float b) { return fabsf(a - b) < 1e-6f; }

int main(void) {
	/* the allowed list */
	CHECK(fr_allowed(NULL) == (FR_30 | FR_60));                  /* no list: what every game had */
	CHECK(fr_allowed("") == (FR_30 | FR_60));
	CHECK(fr_allowed("30,60,120") == (FR_30 | FR_60 | FR_120));
	CHECK(fr_allowed("30 120") == (FR_30 | FR_120));             /* spaces, and 60 left out */
	CHECK(fr_allowed("30") == FR_30);                            /* a game that stays at its own rate */
	CHECK(fr_allowed("60") == (FR_30 | FR_60));                  /* 30 is always allowed */
	CHECK(fr_allowed("30,90,144") == FR_30);                     /* unknown rates ignored */
	/* the rate a setting gives */
	const int all = FR_30 | FR_60 | FR_120, def = FR_30 | FR_60;
	CHECK(fr_effective(30, all) == 30);
	CHECK(fr_effective(60, all) == 60);
	CHECK(fr_effective(120, all) == 120);
	CHECK(fr_effective(120, def) == 60);                         /* the shared 120 in a game without it */
	CHECK(fr_effective(60, FR_30) == 30);
	CHECK(fr_effective(120, FR_30 | FR_120) == 120);
	CHECK(fr_effective(60, FR_30 | FR_120) == 30);               /* never above the setting */
	/* what the menus list */
	{ int l[3] = {0}; CHECK(fr_list(all, l) == 3 && l[0] == 30 && l[1] == 60 && l[2] == 120); }
	{ int l[3] = {0}; CHECK(fr_list(FR_30, l) == 1 && l[0] == 30); }
	{ int l[3] = {0}; CHECK(fr_list(def, l) == 2 && l[1] == 60); }
	/* the vblank grid */
	CHECK(fr_grid_period_us(30) == 16667 && fr_grid_period_us(60) == 16667);
	CHECK(fr_grid_period_us(120) == 8333);
	CHECK(fr_grid_hz(120) == 120 && fr_grid_hz(60) == 60);
	/* the swap interval */
	CHECK(fr_swap_interval(2, 30) == 2 && fr_swap_interval(0, 30) == 1 && fr_swap_interval(12, 30) == 8);
	CHECK(fr_swap_interval(2, 60) == 1 && fr_swap_interval(2, 120) == 1 && fr_swap_interval(1, 120) == 1);
	/* a frame's time */
	CHECK(near(fr_frame_dt(1, 60), 1.0f / 60) && near(fr_frame_dt(2, 60), 1.0f / 30) && near(fr_frame_dt(5, 60), 1.0f / 30));
	CHECK(near(fr_frame_dt(1, 120), 1.0f / 120));
	CHECK(near(fr_frame_dt(2, 120), 1.0f / 60));
	CHECK(near(fr_frame_dt(4, 120), 1.0f / 30) && near(fr_frame_dt(9, 120), 1.0f / 30) && near(fr_frame_dt(0, 120), 1.0f / 120));
	/* presents a second for a game that asks one vblank a frame and makes every edge: the grid's rate */
	CHECK((int)(1000000 / (fr_grid_period_us(fr_effective(120, all)) * fr_swap_interval(1, 120))) == 120);
	CHECK((int)(1000000 / (fr_grid_period_us(fr_effective(60, all)) * fr_swap_interval(2, 60))) == 59);   /* 16667 us: 59.9988 */
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
