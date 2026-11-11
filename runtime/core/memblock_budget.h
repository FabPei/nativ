/*
 * The console's budget for a game's memory blocks (sceKernelAllocMemBlock): user main memory is 256 MiB, plus the
 * memory-expansion mode the game's param.sfo asks for (ATTRIBUTE2 bits 2-3: +29 / +77 / +109 MiB). CDRAM, physically
 * contiguous memory and the common dialog's partition are separate pools and don't count. A block that doesn't fit
 * fails with SCE_KERNEL_ERROR_NO_MEMORY, as on the console -- games probe how much memory they can have by asking
 * for more than there is and taking the error (Silent Hill: Book of Memories' 'triheap' asks for 512 MiB, then
 * 768 MiB, then 896 MiB; given each, the host's own arena ran out on the third and the run ended).
 * The numbers are the firmware's as Vita3K applies them (vita3k/modules/SceSysmem: get_max_user_memory,
 * takes_from_user; pinned 3ab53c9d).
 */
#ifndef MEMBLOCK_BUDGET_H
#define MEMBLOCK_BUDGET_H
#include <stdint.h>

#define MEMBLOCK_ERROR_NO_MEMORY 0x8002710Bu   /* SCE_KERNEL_ERROR_NO_MEMORY */

/* does a block of this SceKernelMemBlockType come out of the game's user main memory budget? */
static inline int memblock_takes_user(uint32_t type) {
	if ((type & 0x0F000000u) == 0x0E000000u) return 0;   /* the common dialog's partition */
	switch (type & 0x00F00000u) {
	case 0x00400000u: case 0x00800000u: case 0x00A00000u: return 0;   /* CDRAM, physically contiguous, common dialog */
	default: return 1;
	}
}

/* the user main memory budget for a param.sfo ATTRIBUTE2 value (0 when the SFO has none) */
static inline uint64_t memblock_user_budget(uint32_t attribute2) {
	uint64_t b = 256ull << 20;
	switch (attribute2 & 0x0Cu) {
	case 0x4: b += 29ull << 20; break;
	case 0x8: b += 77ull << 20; break;
	case 0xC: b += 109ull << 20; break;
	}
	return b;
}

/* may a block of `size` bytes and `type` be added to the `used` bytes already taken from user memory? */
static inline int memblock_fits(uint32_t type, uint32_t size, uint64_t used, uint64_t budget) {
#ifdef MEMBLOCK_BUDGET_CONTROL   /* the behaviour before the budget (the test's fail-before control): every block fits */
	(void)type; (void)size; (void)used; (void)budget; return 1;
#else
	return !memblock_takes_user(type) || used + size <= budget;
#endif
}
#endif
