/*
 * Synthetic USSE SOP2 fixture (the GPU translator's blend-style sum-of-products contract).
 *
 * SOP2 (docs/usse-isa.md §3.11) is the U8 blend op the game's UI/colour shaders lean on;
 * the software interpreter (usse.c's exec_sop2) implements it, the GPU translator (usse_spirv.c)
 * did not have a case for op 12 (OP_SOP2) and rejected every program using it -- measured in a game's
 * gfxcov gap table as 10 fallback shaders / 26,659 draws stuck on the software rasteriser.
 *
 * The fixture hand-encodes SOP2 instruction words (independent of dec_sop2's bit slicing, checked
 * against it and against Vita3K's USSE matcher bitstring), decodes them through the real decoder,
 * runs the interpreter against an oracle computed here from scratch, and hands the same program to
 * usse_spirv_translate -- fail-before: translation dies with "unsupported ... (op 12)"; pass-after:
 * it succeeds and emits a module. The GPU's rendered output is then validated on the game itself
 * by the accepted reference frame (vitarecomp check).
 *
 * The oracle pins the OBSERVED contract, not the ISA doc's packed-word write: an A/B measured on
 * the real title that writing the packed word's bytes 1-3 (the doc's and Vita3K's SOP2 handler
 * reading) corrupts a game's rendering (full-screen green overlay via the interpreter too),
 * while exec_sop2's byte-0 read/write renders the accepted reference. The semantic divergence is
 * an open investigation -- this fixture locks the behaviour
 * both backends must share meanwhile.
 *
 * Build and run: see README.md in this directory (one cc line, run from this folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <math.h>
#include "usse.h"
#include "usse_internal.h"
#include "usse_spirv.h"

/* ---- the runtime usse.c expects (kernel_test.c's pattern) ---- */
uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;   /* rt.h's write-watch: disabled (lo == hi == 0) */
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

static int checks, failed;
static void ck(int cond, const char *what, unsigned long long a, unsigned long long b) {
	checks++;
	if (!cond) { failed++; printf("  FAIL %s: got %#llx want %#llx\n", what, a, b); }
}

/* ---- synthetic GXP: header fields (usse.c's H_* layout) + one primary instruction ---- */
#define GXP_ADDR 0x10000000u
#define H_FLAGS 20
#define H_PRIM_REGS 48
#define H_PRIM_INSTR_COUNT 60
#define H_PRIM_OFF 64
#define H_SEC_INSTR_COUNT 68
#define H_SEC_OFF 72
#define H_SEC_OFF_END 76
#define H_LITERAL_SIZE 88
#define H_LITERAL_DATA_OFF 104
#define CODE_OFF 128

static void put32(uint32_t addr, uint32_t v) { memcpy(recomp_mem + addr, &v, 4); }
static void put64(uint32_t addr, uint64_t v) { memcpy(recomp_mem + addr, &v, 8); }

/* SOP2, fields per docs/usse-isa.md §3.11's table (bit 63 first) */
static uint64_t sop2_word(int pred, int cmod1, int asel1, int dst_ext, int end, int s1_ext, int s2_ext,
                          int cmod2, int count, int amod1, int asel2, int csel1, int csel2, int amod2,
                          int dst_bank, int s1_bank, int s2_bank, int dst_n, int src1_mod, int cop,
                          int aop, int s1_n, int s2_n) {
	uint64_t w = 0x10ull << 59;
	w |= (uint64_t)(pred & 3) << 57;
	w |= (uint64_t)(cmod1 & 1) << 56;
	w |= (uint64_t)(asel1 & 3) << 52;
	w |= (uint64_t)(dst_ext & 1) << 51;
	w |= (uint64_t)(end & 1) << 50;
	w |= (uint64_t)(s1_ext & 1) << 49;
	w |= (uint64_t)(s2_ext & 1) << 48;
	w |= (uint64_t)(cmod2 & 1) << 47;
	w |= (uint64_t)(count & 7) << 44;
	w |= (uint64_t)(amod1 & 1) << 43;
	w |= (uint64_t)(asel2 & 3) << 41;
	w |= (uint64_t)(csel1 & 7) << 38;
	w |= (uint64_t)(csel2 & 7) << 35;
	w |= (uint64_t)(amod2 & 1) << 34;
	w |= (uint64_t)(dst_bank & 3) << 32;
	w |= (uint64_t)(s1_bank & 3) << 30;
	w |= (uint64_t)(s2_bank & 3) << 28;
	w |= (uint64_t)(dst_n & 0x7F) << 21;
	w |= (uint64_t)(src1_mod & 1) << 20;
	w |= (uint64_t)(cop & 3) << 18;
	w |= (uint64_t)(aop & 3) << 16;
	w |= (uint64_t)(s1_n & 0x7F) << 7;
	w |= (uint64_t)(s2_n & 0x7F);
	return w;
}

static void build_gxp(uint64_t insn) {
	memset(recomp_mem + GXP_ADDR, 0, 256);
	put32(GXP_ADDR + 8, 256);                       /* GXP size (usse_gxp_hash) */
	put32(GXP_ADDR + H_FLAGS, 1);                   /* fragment program */
	put32(GXP_ADDR + H_PRIM_REGS, 16);              /* PA/TEMP register words hint (RD16 at 48; bytes 50-51 stay 0) */
	put32(GXP_ADDR + H_PRIM_INSTR_COUNT, 1);
	put32(GXP_ADDR + H_PRIM_OFF, CODE_OFF - H_PRIM_OFF);
	put32(GXP_ADDR + H_SEC_INSTR_COUNT, 0);
	put32(GXP_ADDR + H_SEC_OFF, 0);
	put32(GXP_ADDR + H_SEC_OFF_END, 0);
	put32(GXP_ADDR + H_LITERAL_SIZE, 0);
	put32(GXP_ADDR + H_LITERAL_DATA_OFF, 0);
	put64(GXP_ADDR + CODE_OFF, insn);
	(void)0;
}

/* ---- the OBSERVED SOP2 contract (usse.c's exec_sop2 as-built), computed from scratch ----
 *
 * read_i32(.., 0) takes byte 0 of each U8 operand word and raw-negates it for src1_mod (negabs&1);
 * u8_unpack then reads that 32-bit read value's four bytes; exec_sop2's float complement applies
 * on top (the observed double modification); u8_pack4(out) is computed in full but only byte 0
 * reaches the destination word (write_raw_lane's lane-0 insert), bytes 1-3 preserved (0 here).
 * An A/B measured that writing all four packed bytes (docs/usse-isa.md §3.11 and Vita3K's
 * SOP2 handler) breaks that game's rendering; the divergence stays an open investigation. */
static uint32_t expect_sop2(uint32_t w1, uint32_t w2, int csel1, int cmod1, int csel2, int cmod2,
                            int asel1, int amod1, int asel2, int amod2, int src1_mod, int cop, int aop) {
	uint32_t v1 = w1 & 0xFF, v2 = w2 & 0xFF;
	if (src1_mod) v1 = (uint32_t)(-(int32_t)v1);   /* read_i32's negabs&1 raw negate */
	double s1[4] = {(v1 & 0xFF) / 255.0, ((v1 >> 8) & 0xFF) / 255.0, ((v1 >> 16) & 0xFF) / 255.0, ((v1 >> 24) & 0xFF) / 255.0};
	double s2[4] = {(v2 & 0xFF) / 255.0, ((v2 >> 8) & 0xFF) / 255.0, ((v2 >> 16) & 0xFF) / 255.0, ((v2 >> 24) & 0xFF) / 255.0};
	if (src1_mod) for (int k = 0; k < 4; k++) s1[k] = 1 - s1[k];   /* exec_sop2's complement */
	double out[4];
	for (int k = 0; k < 3; k++) {
		double f1 = csel1 == 1 ? s1[k] : csel1 == 2 ? s2[k] : csel1 == 3 ? s1[3] : csel1 == 4 ? s2[3] : 0;
		double f2 = csel2 == 1 ? s1[k] : csel2 == 2 ? s2[k] : csel2 == 3 ? s1[3] : csel2 == 4 ? s2[3] : 0;
		if (cmod1) f1 = 1 - f1;
		if (cmod2) f2 = 1 - f2;
		double x = f1 * s1[k], y = f2 * s2[k];
		out[k] = cop == 0 ? x + y : cop == 1 ? x - y : cop == 2 ? (x < y ? x : y) : (x > y ? x : y);
	}
	{
		double f1 = asel1 == 1 ? s1[3] : asel1 == 2 ? s2[3] : 0;
		double f2 = asel2 == 1 ? s1[3] : asel2 == 2 ? s2[3] : 0;
		if (amod1) f1 = 1 - f1;
		if (amod2) f2 = 1 - f2;
		double x = f1 * s1[3], y = f2 * s2[3];
		out[3] = aop == 0 ? x + y : aop == 1 ? x - y : aop == 2 ? (x < y ? x : y) : (x > y ? x : y);
	}
	uint32_t packed = 0;
	for (int k = 0; k < 4; k++) {
		double v = out[k] < 0 ? 0 : out[k] > 1 ? 1 : out[k];
		packed |= (uint32_t)(v * 255.0 + 0.5) << (8 * k);
	}
	return packed & 0xFF;   /* byte 0 lands in the destination word; bytes 1-3 are preserved (0 here) */
}

struct Case { const char *name; int csel1, cmod1, csel2, cmod2, asel1, amod1, asel2, amod2, src1_mod, cop, aop, count; uint32_t w1, w2; };

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }

	static const struct Case cases[] = {
		/* name                                   csel1 c1 csel2 c2 as1 a1 as2 a2 sm cop aop cnt  w1        w2 */
		{"src1.rgb*src1 + src2.rgb*src2, add",        1, 0,    2, 0,  1, 0,  2, 0, 0,  0,  0, 0, 0x40201008, 0x306090C0},
		{"src-alpha blend (a1*s1 + (1-a2)*s2), add",  3, 0,    4, 1,  1, 0,  1, 1, 0,  0,  0, 0, 0xFF804020, 0x400000C0},
		{"complemented s1, subtract, max",            1, 0,    2, 0,  2, 0,  1, 1, 1,  1,  3, 0, 0x10203040, 0x50607080},
		{"zero factors, min, a=0",                    0, 0,    0, 0,  0, 0,  0, 0, 0,  2,  0, 0, 0x90A0B0C0, 0xD0E0F000},
		{"s1.aaa factor into rgb, repeat 2",          3, 1,    3, 1,  1, 0,  1, 0, 0,  0,  0, 1, 0x20406080, 0x10305070},
	};

	for (unsigned ci = 0; ci < sizeof cases / sizeof cases[0]; ci++) {
		const struct Case *c = &cases[ci];
		uint64_t insn = sop2_word(0, c->cmod1, c->asel1, 0, 1, 0, 0, c->cmod2, c->count, c->amod1, c->asel2,
		                          c->csel1, c->csel2, c->amod2, 0 /*TEMP*/, 0, 0, 4 /*dst r4*/, c->src1_mod,
		                          c->cop, c->aop, 8 /*s1 r8*/, 12 /*s2 r12*/);
		build_gxp(insn);

		/* 1. the hand-encoded word decodes as SOP2 with the fields above (independent encoding check) */
		UsseProgram *p = usse_decode_uncached(GXP_ADDR);
		int n = 0;
		const UOp *code = usse_program_code(p, 0, &n);
		char what[128];
		snprintf(what, sizeof what, "[%s] decode n", c->name);
		ck(n == 1, what, (unsigned long long)n, 1);
		snprintf(what, sizeof what, "[%s] decode op", c->name);
		ck(code && code[0].op == OP_SOP2, what, code ? code[0].op : 99, OP_SOP2);
		if (code) {
			snprintf(what, sizeof what, "[%s] decode cop/aop", c->name);
			ck(code[0].sub == c->cop && code[0].sub2 == c->aop, what,
			   (unsigned long long)((code[0].sub << 4) | code[0].sub2), (unsigned long long)((c->cop << 4) | c->aop));
			snprintf(what, sizeof what, "[%s] decode reps", c->name);
			ck(code[0].reps == c->count + 1, what, code[0].reps, c->count + 1);
			snprintf(what, sizeof what, "[%s] decode banks", c->name);
			ck(code[0].dst.bank == BK_TEMP && code[0].s1.bank == BK_TEMP && code[0].s2.bank == BK_TEMP &&
			   code[0].dst.num == 4 && code[0].s1.num == 8 && code[0].s2.num == 12, what,
			   (unsigned long long)code[0].dst.num, 4);
			printf("    decoded: raw=%#llx args=%#x/%#x/%#x/%#x s1.negabs=%#x mask=%#x reps=%u mode=%u\n",
			       (unsigned long long)code[0].raw, code[0].args[0], code[0].args[1], code[0].args[2], code[0].args[3],
			       code[0].s1.negabs, code[0].mask, code[0].reps, code[0].repmode);
		}

		/* 2. the interpreter must match the observed-contract oracle (bytes in, byte-0 word out) */
		UsseInst inst; memset(&inst, 0, sizeof inst);
		UsseDraw draw; memset(&draw, 0, sizeof draw);
		for (int r = 0; r <= (c->count ? 1 : 0); r++) {
			inst.r[8 + r].u = r ? 0x01020304u : c->w1;   /* repeat 2 reads r8/r9 and r12/r13 (SLMSI identity stepping) */
			inst.r[12 + r].u = r ? 0x05060708u : c->w2;
		}
		usse_run_primary(p, &inst, &draw, 0, 0);
		printf("    run: r4..r7 = %#x %#x %#x %#x\n", inst.r[4].u, inst.r[5].u, inst.r[6].u, inst.r[7].u);
		uint32_t want = expect_sop2(c->w1, c->w2, c->csel1, c->cmod1, c->csel2, c->cmod2, c->asel1, c->amod1,
		                            c->asel2, c->amod2, c->src1_mod, c->cop, c->aop);
		snprintf(what, sizeof what, "[%s] interpreter word 0", c->name);
		ck(inst.r[4].u == want, what, inst.r[4].u, want);
		if (c->count) {
			uint32_t want1 = expect_sop2(0x01020304u, 0x05060708u, c->csel1, c->cmod1, c->csel2, c->cmod2, c->asel1,
			                             c->amod1, c->asel2, c->amod2, c->src1_mod, c->cop, c->aop);
			snprintf(what, sizeof what, "[%s] interpreter word 1", c->name);
			ck(inst.r[5].u == want1, what, inst.r[5].u, want1);
		}
		usse_program_free(p);

		/* 3. the GPU translator must accept the same program (fail-before: dies at "op 12") */
		p = usse_decode_uncached(GXP_ADDR);
		usse_spirv_set_canonical_varyings(1);
		uint32_t sa[256] = {0}; int units[16] = {0}, nsmp = 0;
		SpirvModule m = usse_spirv_translate(p, 0, usse_fragment_layout(p), sa, 16, units, &nsmp, 0, 0, 0, 0);
		if (getenv("SOP2_DUMP") && m.ok) {   /* the emitted module for spirv structure checks */
			FILE *f = fopen(getenv("SOP2_DUMP"), "wb");
			if (f) { fwrite(m.words ? m.words : (const void *)&m, 4, m.nwords, f); fclose(f); }
		}
		snprintf(what, sizeof what, "[%s] translate ok", c->name);
		ck(m.ok, what, (unsigned long long)m.ok, 1);
		if (!m.ok) printf("    err: %s\n", m.err);
		else {
			snprintf(what, sizeof what, "[%s] module words", c->name);
			ck(m.nwords > 0, what, m.nwords, 1);
		}
		usse_spirv_free(&m);
		usse_program_free(p);
	}

	printf("%d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
