/* seedaddr_test.c -- which missed call is recorded as a seed (runtime/core/seedaddr.h, see its header comment).
 * Fail-before: -DSEEDADDR_BEFORE is the old rule (0x81000000..0xa0000000 only).
 *
 * Run (WSL, from this folder):
 *   cc -O1 -Wall -I ../core seedaddr_test.c -o /tmp/seedaddr_test && /tmp/seedaddr_test
 *   cc -O1 -Wall -I ../core -DSEEDADDR_BEFORE seedaddr_test.c -o /tmp/seedaddr_before && /tmp/seedaddr_before
 */
#include <stdio.h>
#include "seedaddr.h"
#ifdef SEEDADDR_BEFORE
#define seed_addr(a, lo, hi, n) ((a) >= 0x81000000u && (a) < 0xa0000000u)
#endif

static int fails;
static void check(const char *what, int ok) { printf("%-72s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	/* synthetic images: the main program, a module in the first slots, one at slot 0x3d (0xc5000000) and its slots */
	const uint32_t lo[] = {0x81000000u, 0x88000000u, 0xc5000000u, 0xc5001000u, 0xffe25800u};
	const uint32_t hi[] = {0x81465000u, 0x88052000u, 0xc50005b0u, 0xc5001100u, 0xffe26800u};
	const int n = 5;
	check("main program code is a seed", seed_addr(0x81044f3du, lo, hi, n));
	check("a module in the first slots is a seed", seed_addr(0x88001235u, lo, hi, n));
	check("a module's code at 0xc5000000 (past 0xa0000000) is a seed", seed_addr(0xc50002f7u, lo, hi, n));
	check("  ARM (even) address there too", seed_addr(0xc5000300u, lo, hi, n));
	check("past that module's end is not", !seed_addr(0xc50005b0u, lo, hi, n));
	check("a null call is not", !seed_addr(0, lo, hi, n));
	check("a poisoned pointer (0xdeadbeef, no image) is not", !seed_addr(0xdeadbeefu, lo, hi, n));
	check("an imported-variable slot is not", !seed_addr(0xffe25801u, lo, hi, n));
	if (fails) { printf("%d checks FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
