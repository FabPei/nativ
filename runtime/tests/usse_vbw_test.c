/*
 * Synthetic USSE VBW fixture: the bitwise op's repeat count.
 *
 * VBW (docs/usse-isa.md §3.10; Vita3K's matcher "01ooopppsnrydecxaaaaittttthhbwkkffggjjjjjjjlllllllmmmmmmmqqqqqqq")
 * carries a 4-bit repeat count at bits 47:44 (+1); Vita3K's vbw runs BEGIN_REPEAT(repeat_count) in SLMSI mode with
 * multiplier 1 -- one 32-bit word a step. dec_vbw set reps = 1 and ignored it, so `or dst, src, #0` with repeat 2
 * (a compiler's two-word copy, e.g. four halves of a uniform into an output pair) copied one word; the second kept
 * whatever it held before. Fail-before: "decode reps" and the "word 1.." checks; pass-after: every word copied.
 *
 * The fixture hand-encodes VBW words, decodes them with the real decoder, runs the interpreter against an oracle
 * computed here, and hands the same program to usse_spirv_translate (it must still translate).
 *
 * Build and run (WSL, from this folder):
 *   cc -O1 -w -D_GNU_SOURCE -I ../gfx -I ../core -I ../../recompiler -I ../.. usse_vbw_test.c ../gfx/usse.c \
 *      ../gfx/usse_spirv.c -o /tmp/usse_vbw_test -lm && /tmp/usse_vbw_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "usse.h"
#include "usse_internal.h"
#include "usse_spirv.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

static int checks, failed;
static void ck(int cond, const char *what, unsigned long long a, unsigned long long b) {
	checks++;
	if (!cond) { failed++; printf("  FAIL %s: got %#llx want %#llx\n", what, a, b); }
	else printf("  ok   %s\n", what);
}

#define GXP_ADDR 0x10000000u
#define CODE_OFF 128
static void put32(uint32_t addr, uint32_t v) { memcpy(recomp_mem + addr, &v, 4); }
static void put64(uint32_t addr, uint64_t v) { memcpy(recomp_mem + addr, &v, 8); }
static void build_gxp(uint64_t insn) {
	memset(recomp_mem + GXP_ADDR, 0, 256);
	put32(GXP_ADDR + 8, 256);                   /* size */
	put32(GXP_ADDR + 20, 1);                    /* fragment program */
	put32(GXP_ADDR + 48, 16);                   /* PA/TEMP words hint */
	put32(GXP_ADDR + 60, 1);                    /* primary instruction count */
	put32(GXP_ADDR + 64, CODE_OFF - 64);        /* primary offset */
	put64(GXP_ADDR + CODE_OFF, insn);
}

/* VBW, fields per Vita3K's bitstring (bit 63 first). Banks: dst 2-bit + ext, src 2-bit + ext (TEMP = 0/0, IMM = 2/1) */
static uint64_t vbw_word(int op1, int op2, int count, int end, int dst_bank, int dst_n, int s1_bank, int s1_n,
                         int s2_bank, int s2_ext, int s2_n, int src2_sel) {
	uint64_t w = 1ull << 62;
	w |= (uint64_t)(op1 & 7) << 59;
	w |= (uint64_t)(end & 1) << 50;
	w |= (uint64_t)(s2_ext & 1) << 48;
	w |= (uint64_t)(count & 15) << 44;
	w |= (uint64_t)(op2 & 1) << 35;
	w |= (uint64_t)(dst_bank & 3) << 32;
	w |= (uint64_t)(s1_bank & 3) << 30;
	w |= (uint64_t)(s2_bank & 3) << 28;
	w |= (uint64_t)(dst_n & 0x7F) << 21;
	w |= (uint64_t)(src2_sel & 0x7F) << 14;
	w |= (uint64_t)(s1_n & 0x7F) << 7;
	w |= (uint64_t)(s2_n & 0x7F);
	return w;
}

struct Case { const char *name; int op1, op2, count, s2_imm; uint32_t imm; };

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }
	static const struct Case cases[] = {
		/* name                                   op1 op2 cnt imm? imm */
		{"or r4, r8, #0 (a move), repeat 1",        2, 1, 0, 1, 0},
		{"or r4, r8, #0, repeat 2 (two-word copy)", 2, 1, 1, 1, 0},
		{"and r4, r8, r12, repeat 4",               2, 0, 3, 0, 0},
		{"xor r4, r8, #0x5a, repeat 3",             3, 0, 2, 1, 0x5a},
	};
	for (unsigned ci = 0; ci < sizeof cases / sizeof cases[0]; ci++) {
		const struct Case *c = &cases[ci];
		uint64_t insn = c->s2_imm ? vbw_word(c->op1, c->op2, c->count, 1, 0, 4, 0, 8, 2, 1, (int)(c->imm & 0x7F), (int)(c->imm >> 7))
		                          : vbw_word(c->op1, c->op2, c->count, 1, 0, 4, 0, 8, 0, 0, 12, 0);
		build_gxp(insn);
		char what[160];
		UsseProgram *p = usse_decode_uncached(GXP_ADDR);
		int n = 0;
		const UOp *code = usse_program_code(p, 0, &n);
		snprintf(what, sizeof what, "[%s] decodes as one VBW", c->name);
		ck(n == 1 && code && code[0].op == OP_VBW, what, code ? code[0].op : 99, OP_VBW);
		if (!code || n != 1) { usse_program_free(p); continue; }
		snprintf(what, sizeof what, "[%s] decode reps", c->name);
		ck(code[0].reps == c->count + 1, what, code[0].reps, c->count + 1);

		UsseInst inst; memset(&inst, 0, sizeof inst);
		UsseDraw draw; memset(&draw, 0, sizeof draw);
		for (int k = 0; k < 4; k++) { inst.r[4 + k].u = 0xDEAD0000u + k; inst.r[8 + k].u = 0x11223344u * (k + 1); inst.r[12 + k].u = 0x0F0FF0F0u >> k; }
		usse_run_primary(p, &inst, &draw, 0, 0);
		for (int k = 0; k < 4; k++) {
			uint32_t b = c->s2_imm ? c->imm : inst.r[12 + k].u, a = 0x11223344u * (k + 1);
			uint32_t want = k > c->count ? 0xDEAD0000u + k : c->op1 == 3 ? a ^ b : c->op2 ? a | b : a & b;
			snprintf(what, sizeof what, "[%s] word %d%s", c->name, k, k > c->count ? " untouched" : "");
			ck(inst.r[4 + k].u == want, what, inst.r[4 + k].u, want);
		}
		usse_program_free(p);

		p = usse_decode_uncached(GXP_ADDR);
		usse_spirv_set_canonical_varyings(1);
		uint32_t sa[256] = {0}; int units[16] = {0}, nsmp = 0;
		SpirvModule m = usse_spirv_translate(p, 0, usse_fragment_layout(p), sa, 16, units, &nsmp, 0, 0, 0, 0);
		snprintf(what, sizeof what, "[%s] translates", c->name);
		ck(m.ok && m.nwords > 0, what, (unsigned long long)m.ok, 1);
		if (!m.ok) printf("    err: %s\n", m.err);
		usse_spirv_free(&m);
		usse_program_free(p);
	}
	printf("%d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
