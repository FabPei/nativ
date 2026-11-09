/* seedaddr.h -- which call to code that is neither recompiled nor an import is a missed function (main.c
 * recomp_call_external records those for analyze.py --seeds; runtime/tests/seedaddr_test.c).
 *
 * A missed function lives in a loaded image: the main program (0x81000000 up) or a module, which the pipeline places
 * at 0x88000000 + 16 MB each (vitarecomp.py mod_base), so a game with many on-demand modules (LOAD_MODULES) has
 * code well past 0xa0000000. Before: only 0x81000000..0xa0000000 counted, so a module's missed function up there
 * stopped the run without a seed and bootstrap could not get past it (NIGHT-01 batch B, a game module at 0xc5000000).
 * Not a missed function: a call through a null or poisoned pointer (outside every image), or into the
 * imported-variable slots (0xffe00000 up, data).
 */
#ifndef RECOMP_SEEDADDR_H
#define RECOMP_SEEDADDR_H
#include <stdint.h>

static inline int seed_addr(uint32_t addr, const uint32_t *lo, const uint32_t *hi, int n) {
	const uint32_t a = addr & ~1u;
	if (a >= 0x81000000u && a < 0xa0000000u) return 1;   /* the main program and the first modules (as before) */
	if (a >= 0xffe00000u) return 0;                      /* imported-variable slots */
	for (int i = 0; i < n; i++) if (a >= lo[i] && a < hi[i]) return 1;   /* a loaded image's segment */
	return 0;
}
#endif
