/* the freed memory of a game's memory blocks (sysmem.c's sceKernelAllocMemBlock / FreeMemBlock), kept for the next block
 * that fits; a header of its own so that runtime/tests/memblock_budget_test.c checks it as sysmem.c uses it */
#ifndef MEMBLOCK_HOLES_H
#define MEMBLOCK_HOLES_H
#include <stdint.h>
/* a freed block's guest memory, for the next block that fits (best fit, split; neighbours merged). guest_alloc is a bump
 * allocator with no free, and it used to be the only source: Killzone takes ~200 MB of new blocks at every level load and
 * movie (its render-target pools, VM heaps) and frees the old ones, so a long session walked the 1.75 GB heap towards
 * "guest heap exhausted" -- its level 1 -> 2 transition on Linux had reached 0x3b5..., five or six loads from the end. A
 * console's kernel reuses the memory the same way. Bases and sizes are page-aligned (sizes rounded up: the next block
 * starts at a page boundary past the end anyway). */
#define MAX_HOLES 1024
static uint32_t holes[MAX_HOLES][2];
static int nholes;
static void hole_put(uint32_t base, uint32_t size);
/* a hole for `size` bytes at an `align`-aligned base (page or larger; best fit), split: what lies before the aligned base
 * and after the block stays a hole. Aligned blocks used to come only from guest_alloc: Silent Hill: Book of Memories probes
 * its memory with 1 MiB-aligned blocks it frees again (128, 192, 224 ... 254 MiB), and each probe took fresh arena until
 * the run ended with "guest heap exhausted" */
static uint32_t hole_take(uint32_t size, uint32_t align) {
	int best = -1; uint32_t best_at = 0;
	for (int i = 0; i < nholes; i++) {
		const uint64_t at = ((uint64_t)holes[i][0] + align - 1) & ~(uint64_t)(align - 1);
		if (at + size > (uint64_t)holes[i][0] + holes[i][1]) continue;
		if (best < 0 || holes[i][1] < holes[best][1]) { best = i; best_at = (uint32_t)at; }
	}
#ifdef MEMBLOCK_HOLES_CONTROL   /* the behaviour before (the test's fail-before control): holes served page-aligned blocks only */
	if (align > 4096) return 0;
#endif
	if (best < 0) return 0;
	const uint32_t h0 = holes[best][0], h1 = holes[best][0] + holes[best][1];
	holes[best][0] = holes[--nholes][0]; holes[best][1] = holes[nholes][1];   /* (taken out; its unused ends go back) */
	if (best_at > h0) hole_put(h0, best_at - h0);
	if (best_at + size < h1) hole_put(best_at + size, h1 - (best_at + size));
	return best_at;
}
static void hole_put(uint32_t base, uint32_t size) {
	for (int i = 0; i < nholes; i++) {   /* merged with a neighbour on either side (then the merged hole is put back) */
		if (holes[i][0] + holes[i][1] == base || base + size == holes[i][0]) {
			if (holes[i][0] < base) base = holes[i][0];
			size += holes[i][1];
			holes[i][0] = holes[--nholes][0]; holes[i][1] = holes[nholes][1];
			hole_put(base, size); return;
		}
	}
	if (nholes < MAX_HOLES) { holes[nholes][0] = base; holes[nholes][1] = size; nholes++; }   /* (else that memory stays unused) */
}
static uint32_t page_up(uint32_t n) { return (n + 4095u) & ~4095u; }
#endif
