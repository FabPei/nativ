/*
 * memblock_budget_test: the console's memory-block budget (core/memblock_budget.h, used by sysmem.c's
 * sceKernelAllocMemBlock). User main memory is 256 MiB plus the param.sfo's expansion mode; CDRAM, physically contiguous
 * memory and the common dialog's partition are pools of their own; a block over the budget fails with
 * SCE_KERNEL_ERROR_NO_MEMORY. The replayed sequence is Silent Hill: Book of Memories' memory probe (its 'triheap' asks for
 * 512 MiB, 768 MiB, 896 MiB of USER_RW): every one is more than a console has, so every one must be refused -- given
 * them, the host arena ran out on the third and the run ended before the first frame.
 *
 * Build and run (fail-before control: -DMEMBLOCK_BUDGET_CONTROL, the old "every block fits"; expected to fail):
 *   cc -O1 -g -Wall -I ../core memblock_budget_test.c -o /tmp/memblock_budget_test && /tmp/memblock_budget_test
 */
#include <stdio.h>
#include "memblock_budget.h"
#include "memblock_holes.h"

/* sysmem.c's block allocation on a fake bump arena (guest_alloc): a hole first, else fresh arena */
static uint64_t arena_top = 0x10000000u;
static uint32_t take(uint32_t size, uint32_t align) {
	uint32_t b = hole_take(page_up(size), align);
	if (b) return b;
	arena_top = (arena_top + align - 1) & ~(uint64_t)(align - 1);
	b = (uint32_t)arena_top; arena_top += size; return b;
}

static int fails, checks;
static void check(const char *what, int ok) { checks++; if (!ok) { fails++; printf("FAIL  %s\n", what); } else printf("ok    %s\n", what); }

#define MiB(n) ((uint64_t)(n) << 20)
enum { USER_RW = 0x0c20d060u, USER_RWDATA = 0x0c208060u, CDRAM_RW = 0x09408060u, PHYCONT_RW = 0x0c80d060u, PHYCONT_NC_RW = 0x0d808060u };

int main(void) {
	const uint64_t b0 = memblock_user_budget(0);
	check("budget without expansion is 256 MiB", b0 == MiB(256));
	check("ATTRIBUTE2 0x4: +29 MiB", memblock_user_budget(0x4) == MiB(285));
	check("ATTRIBUTE2 0x8: +77 MiB", memblock_user_budget(0x8) == MiB(333));
	check("ATTRIBUTE2 0xC: +109 MiB", memblock_user_budget(0xC) == MiB(365));
	check("other ATTRIBUTE2 bits do not change it", memblock_user_budget(0x3) == MiB(256));

	check("USER_RW takes user memory", memblock_takes_user(USER_RW));
	check("USER_RWDATA takes user memory", memblock_takes_user(USER_RWDATA));
	check("CDRAM is its own pool", !memblock_takes_user(CDRAM_RW));
	check("PHYCONT is its own pool", !memblock_takes_user(PHYCONT_RW));
	check("PHYCONT_NC is its own pool", !memblock_takes_user(PHYCONT_NC_RW));

	/* the memory probe: each request on its own is more than the console's user memory */
	uint64_t used = MiB(1);   /* (the libc heap block the game made first) */
	check("probe 512 MiB refused", !memblock_fits(USER_RW, 0x20000000u, used, b0));
	check("probe 768 MiB refused", !memblock_fits(USER_RW, 0x30000000u, used, b0));
	check("probe 896 MiB refused", !memblock_fits(USER_RW, 0x38000000u, used, b0));
	check("a 200 MiB heap fits", memblock_fits(USER_RW, (uint32_t)MiB(200), used, b0));
	used += MiB(200);
	check("then 60 MiB more does not (201 + 60 > 256)", !memblock_fits(USER_RW, (uint32_t)MiB(60), used, b0));
	check("but 55 MiB does (exactly up to the budget)", memblock_fits(USER_RW, (uint32_t)MiB(55), used, b0));
	check("CDRAM is not limited by user memory", memblock_fits(CDRAM_RW, (uint32_t)MiB(100), used, b0));
	check("with +109 MiB expansion 60 MiB more fits", memblock_fits(USER_RW, (uint32_t)MiB(60), used, memblock_user_budget(0xC)));
	check("the error is SCE_KERNEL_ERROR_NO_MEMORY", MEMBLOCK_ERROR_NO_MEMORY == 0x8002710Bu);

	/* the probe's second half: 1 MiB-aligned blocks of 128, 192 ... 254 MiB, each freed again before the next. The freed
	 * memory serves the next (aligned) block, so the arena grows by about the two first sizes, not by their sum (1.5 GiB) */
	const uint32_t probe[] = {128, 192, 224, 240, 248, 252, 254};
	const uint64_t top0 = arena_top;
	for (unsigned k = 0; k < sizeof probe / sizeof *probe; k++) {
		const uint32_t sz = (uint32_t)MiB(probe[k]), b = take(sz, 0x100000u);
		if (b & 0xfffffu) { check("probe block 1 MiB-aligned", 0); break; }
		hole_put(b, page_up(sz));
	}
	printf("      arena grew by %llu MiB over the probe\n", (unsigned long long)((arena_top - top0) >> 20));
	check("freed aligned blocks are reused (arena growth <= 128 + 192 MiB + 1 MiB)", arena_top - top0 <= MiB(321));
	const uint32_t a = take((uint32_t)MiB(16), 0x100000u), p = take(4096u * 3, 4096u);
	check("an aligned block from a hole is aligned", !(a & 0xfffffu));
	check("a page block still comes from a hole", p >= top0 && p < arena_top);

	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
