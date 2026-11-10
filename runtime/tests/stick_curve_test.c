/* stick_curve_test.c -- runtime/ui/stick_curve.h (UI-04b): a stick's SDL axes -> the Vita's stick bytes with the player's
 * sensitivity and dead zone. Checks: the defaults (1x, 0 %) give (axis + 32768) >> 8 for every axis value, whatever the
 * other axis is; the centre is 128; inside the dead zone the stick is centred, just past it barely moved (no jump); full
 * tilt is 0 / 255 with a dead zone; a gain above 1 reaches full tilt sooner and clamps (never wraps), below 1 never gets
 * there; the bytes never go down as the stick goes further; left and right mirror.
 * Build: cc -O1 -g -Wall -I ../ui stick_curve_test.c -lm -o /tmp/stick_curve_test && /tmp/stick_curve_test */
#include <stdio.h>
#include <stdlib.h>
#include "stick_curve.h"

static int fails, checks;
static void check(const char *what, int ok) { checks++; if (!ok) { fails++; printf("FAILED: %s\n", what); } }

int main(void) {
	/* 1. the defaults: byte for byte the old mapping, every axis value, the other axis anywhere */
	{	static const int others[] = {-32768, -16384, -1, 0, 1, 12345, 32767};
		long bad = 0;
		for (int a = -32768; a <= 32767; a++)
			for (size_t o = 0; o < sizeof others / sizeof others[0]; o++) {
				uint8_t x, y; stick_curve(a, others[o], 1.0f, 0.0f, &x, &y);
				if (x != (uint8_t)((a + 32768) >> 8) || y != (uint8_t)((others[o] + 32768) >> 8)) bad++;
				stick_curve(others[o], a, 1.0f, 0.0f, &x, &y);
				if (y != (uint8_t)((a + 32768) >> 8) || x != (uint8_t)((others[o] + 32768) >> 8)) bad++;
			}
		check("defaults (1x, 0 %): every axis value gives (axis + 32768) >> 8", bad == 0);
		if (bad) printf("    %ld differing\n", bad);
	}
	uint8_t x, y;
	/* 2. the centre */
	stick_curve(0, 0, 1.5f, 0.15f, &x, &y); check("centre with a curve: 128, 128", x == 128 && y == 128);
	stick_curve(0, 0, 0.5f, 0.0f, &x, &y); check("centre at 0.5x: 128, 128", x == 128 && y == 128);
	/* 3. the dead zone: inside centred, just past it barely moved, full tilt full */
	stick_curve(3000, -2000, 1.0f, 0.15f, &x, &y); check("inside a 15 % dead zone (about 11 %): centred", x == 128 && y == 128);
	stick_curve(-4800, 0, 1.0f, 0.15f, &x, &y); check("just past a 15 % dead zone (about 14.6 % -> still in): centred", x == 128);
	stick_curve(5200, 0, 1.0f, 0.15f, &x, &y); check("just past a 15 % dead zone (about 15.9 %): within 2 of the centre", x >= 128 && x <= 130);
	stick_curve(32767, 0, 1.0f, 0.2f, &x, &y); check("full right with a 20 % dead zone: 255", x == 255 && y == 128);
	stick_curve(-32768, 0, 1.0f, 0.2f, &x, &y); check("full left with a 20 % dead zone: 0", x == 0 && y == 128);
	stick_curve(0, 32767, 1.0f, 0.3f, &x, &y); check("full down with a 30 % dead zone: 255", y == 255 && x == 128);
	/* 4. the gain */
	stick_curve(16384, 0, 2.0f, 0.0f, &x, &y); check("half tilt at 2x: full (255)", x == 255);
	stick_curve(32767, 32767, 2.0f, 0.0f, &x, &y); check("a corner at 2x: clamped to 255, 255 (no wrap)", x == 255 && y == 255);
	stick_curve(-32768, -32768, 2.0f, 0.1f, &x, &y); check("the other corner at 2x: 0, 0", x == 0 && y == 0);
	stick_curve(32767, 0, 0.5f, 0.0f, &x, &y); check("full tilt at 0.5x: about half (191)", x == 191);
	stick_curve(-32768, 0, 0.5f, 0.0f, &x, &y); check("full left at 0.5x: about half (64)", x == 64);
	/* 5. never down as the stick goes further; left mirrors right */
	{	static const float S[] = {0.5f, 1.0f, 1.3f, 2.0f}, D[] = {0.0f, 0.1f, 0.3f};
		int mono = 1, mirror = 1, range = 1;
		for (int si = 0; si < 4; si++) for (int di = 0; di < 3; di++) {
			int last = -1;
			for (int a = -32768; a <= 32767; a += 7) {
				stick_curve(a, 0, S[si], D[di], &x, &y);
				if (x < last) mono = 0;
				last = x;
				if (y != 128) range = 0;
			}
			for (int a = 256; a <= 32512; a += 256) {   /* (whole bytes: +a and -a sit at 128 + k and 128 - k exactly) */
				uint8_t xp, xn, yy; stick_curve(a, 0, S[si], D[di], &xp, &yy); stick_curve(-a, 0, S[si], D[di], &xn, &yy);
				if (abs((xp - 128) - (128 - xn)) > 1) mirror = 0;
			}
		}
		check("bytes never go down as the stick goes right (4 gains x 3 dead zones)", mono);
		check("a stick moved along x leaves y at 128", range);
		check("left mirrors right (within one byte)", mirror);
	}
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
